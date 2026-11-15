/* Benchmark runner: bench [-k TEXT] [--quick] [--runs N] [--baseline FILE]
 *                         [--write-baseline FILE] NOVELO DIRECTORY
 *
 * Runs every .nvl file of DIRECTORY with the interpreter NOVELO, as a program
 * of the command line, several times each, and reports the median wall time
 * and the peak memory (resident set) of the runs. A program checks its own
 * results and prints exactly "ok\n"; anything else, or an exit code other
 * than 0, is a failure.
 *
 * A comment line at the top, `; scale: N`, makes N the line on standard
 * input, from which the program reads its size.
 *
 *   --runs N               runs per program (default 5); the median is reported
 *   --baseline FILE        also shows the times of FILE and the change
 *   --write-baseline FILE  writes the medians and peaks to FILE
 *   --quick                one run at a hundredth of the scale: only checks
 *                          that the programs still work, as under the sanitizers
 *
 * The times are only reported: a slower program never fails the run.
 */
/* wait4, which reports a child's peak memory, is a BSD extension: glibc
 * hides it under the strict _POSIX_C_SOURCE of the Makefile. */
#define _DEFAULT_SOURCE

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

constexpr int DEFAULT_RUNS = 5;
constexpr int MAXIMUM_RUNS = 101;

typedef struct Program {
    char name[256];
    char path[4096];
    long long scale; /* 0: nothing on standard input */
} Program;

typedef struct Measure {
    bool passed;
    double seconds;
    long long peak_bytes;
} Measure;

typedef struct BaselineEntry {
    char name[256];
    double seconds;
    double megabytes;
} BaselineEntry;

/* ---- the programs ------------------------------------------------------------------ */

static long long read_scale(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "bench: cannot read %s\n", path);
        exit(2);
    }
    long long scale = 0;
    char line[512];
    while (fgets(line, sizeof line, file) && line[0] == ';') {
        long long number;
        if (sscanf(line, "; scale: %lld", &number) == 1) scale = number;
    }
    fclose(file);
    return scale;
}

static int compare_programs(const void *left, const void *right) {
    return strcmp(((const Program *)left)->name, ((const Program *)right)->name);
}

static size_t load_programs(const char *directory, const char *filter, Program **programs) {
    DIR *listing = opendir(directory);
    if (!listing) {
        fprintf(stderr, "bench: cannot open %s\n", directory);
        exit(2);
    }
    size_t count = 0, capacity = 16;
    *programs = calloc(capacity, sizeof(Program));
    for (struct dirent *entry; (entry = readdir(listing));) {
        size_t name_length = strlen(entry->d_name);
        if (name_length < 5 || name_length >= sizeof((Program){}.name)) continue;
        if (strcmp(entry->d_name + name_length - 4, ".nvl") != 0) continue;
        if (filter && !strstr(entry->d_name, filter)) continue;
        if (count == capacity) *programs = realloc(*programs, (capacity *= 2) * sizeof(Program));
        Program *program = &(*programs)[count++];
        snprintf(program->name, sizeof program->name, "%.*s", (int)(name_length - 4), entry->d_name);
        snprintf(program->path, sizeof program->path, "%s/%s", directory, entry->d_name);
        program->scale = read_scale(program->path);
    }
    closedir(listing);
    qsort(*programs, count, sizeof(Program), compare_programs);
    return count;
}

/* ---- one run --------------------------------------------------------------------------- */

static double elapsed(struct timespec start, struct timespec end) {
    return (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) / 1e9;
}

/* Runs NOVELO on the program with its scale on standard input, and gathers
 * its standard output; its standard error passes through. */
static Measure run_once(const char *novelo, const Program *program, long long scale) {
    int input[2], output[2];
    if (pipe(input) != 0 || pipe(output) != 0) {
        perror("pipe");
        exit(2);
    }
    fflush(stdout);
    fflush(stderr);
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        exit(2);
    }
    if (child == 0) {
        dup2(input[0], STDIN_FILENO);
        dup2(output[1], STDOUT_FILENO);
        close(input[0]);
        close(input[1]);
        close(output[0]);
        close(output[1]);
        execl(novelo, novelo, program->path, (char *)nullptr);
        perror(novelo);
        _exit(127);
    }
    close(input[0]);
    close(output[1]);
    /* the scale line is small: it fits in the pipe before the child reads it */
    char line[32];
    int line_length = scale > 0 ? snprintf(line, sizeof line, "%lld\n", scale) : 0;
    if (line_length > 0 && write(input[1], line, (size_t)line_length) != line_length) perror("write");
    close(input[1]);

    /* the first bytes of the output are kept, the rest only drained */
    char text[64], drained[4096];
    size_t text_length = 0;
    for (;;) {
        bool room = text_length < sizeof text - 1;
        ssize_t got = room ? read(output[0], text + text_length, sizeof text - 1 - text_length)
                           : read(output[0], drained, sizeof drained);
        if (got <= 0) break;
        if (room) text_length += (size_t)got;
    }
    text[text_length] = '\0';
    close(output[0]);

    int status = 0;
    struct rusage usage;
    if (wait4(child, &status, 0, &usage) < 0) {
        perror("wait4");
        exit(2);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    bool exited = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    bool printed = strcmp(text, "ok\n") == 0;
    if (exited && !printed) fprintf(stderr, "    output \"%s\", expected \"ok\\n\"\n", text);
    if (WIFSIGNALED(status)) fprintf(stderr, "    crashed with signal %d\n", WTERMSIG(status));
    return (Measure){
        .passed = exited && printed,
        .seconds = elapsed(start, end),
#ifdef __APPLE__
        .peak_bytes = usage.ru_maxrss, /* bytes on macOS */
#else
        .peak_bytes = usage.ru_maxrss * 1024LL, /* kilobytes on Linux */
#endif
    };
}

static int compare_doubles(const void *left, const void *right) {
    double a = *(const double *)left, b = *(const double *)right;
    return (a > b) - (a < b);
}

/* ---- the baseline ---------------------------------------------------------------------- */

/* Lines `name seconds megabytes`; lines starting with # are comments. */
static size_t read_baseline(const char *path, BaselineEntry **entries) {
    *entries = nullptr;
    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "bench: no baseline at %s\n", path);
        return 0;
    }
    size_t count = 0, capacity = 16;
    *entries = calloc(capacity, sizeof(BaselineEntry));
    char line[512];
    while (fgets(line, sizeof line, file)) {
        if (line[0] == '#') continue;
        BaselineEntry entry;
        if (sscanf(line, "%255s %lf %lf", entry.name, &entry.seconds, &entry.megabytes) != 3) continue;
        if (count == capacity) *entries = realloc(*entries, (capacity *= 2) * sizeof(BaselineEntry));
        (*entries)[count++] = entry;
    }
    fclose(file);
    return count;
}

static const BaselineEntry *find_entry(const BaselineEntry *entries, size_t count, const char *name) {
    for (size_t index = 0; index < count; index++) {
        if (strcmp(entries[index].name, name) == 0) return &entries[index];
    }
    return nullptr;
}

static double megabytes(long long bytes) { return (double)bytes / (1024.0 * 1024.0); }

int main(int argument_count, char **arguments) {
    const char *filter = nullptr, *baseline_path = nullptr, *write_path = nullptr;
    const char *novelo = nullptr, *directory = nullptr;
    int runs = DEFAULT_RUNS;
    bool quick = false, usage_error = false;
    for (int index = 1; index < argument_count; index++) {
        const char *argument = arguments[index];
        bool has_value = index + 1 < argument_count;
        if (strcmp(argument, "-k") == 0 && has_value) {
            filter = arguments[++index];
        } else if (strcmp(argument, "--runs") == 0 && has_value) {
            runs = atoi(arguments[++index]);
            if (runs < 1 || runs > MAXIMUM_RUNS) usage_error = true;
        } else if (strcmp(argument, "--baseline") == 0 && has_value) {
            baseline_path = arguments[++index];
        } else if (strcmp(argument, "--write-baseline") == 0 && has_value) {
            write_path = arguments[++index];
        } else if (strcmp(argument, "--quick") == 0) {
            quick = true;
        } else if (argument[0] != '-' && !novelo) {
            novelo = argument;
        } else if (argument[0] != '-' && !directory) {
            directory = argument;
        } else {
            usage_error = true;
        }
    }
    if (usage_error || !directory) {
        fprintf(stderr,
                "usage: %s [-k TEXT] [--quick] [--runs N] [--baseline FILE] [--write-baseline FILE] NOVELO DIRECTORY\n",
                arguments[0]);
        return 2;
    }
    if (quick) runs = 1;

    Program *programs;
    size_t count = load_programs(directory, filter, &programs);
    BaselineEntry *baseline = nullptr;
    size_t baseline_count = baseline_path ? read_baseline(baseline_path, &baseline) : 0;
    FILE *written = nullptr;
    if (write_path) {
        written = fopen(write_path, "w");
        if (!written) {
            fprintf(stderr, "bench: cannot write %s\n", write_path);
            return 2;
        }
        struct utsname system;
        uname(&system);
        fprintf(written, "# make bench: median seconds and peak megabytes of %d runs of each program\n", runs);
        fprintf(written, "# machine: %s %s; regenerate with make bench-baseline\n", system.sysname, system.machine);
    }

    if (!quick) {
        printf("  %-12s %10s %10s %10s %9s %10s %8s\n", "program", "scale", "median s", "spread", "peak MB",
               baseline_count ? "baseline" : "", baseline_count ? "change" : "");
    }
    size_t passed = 0;
    for (size_t index = 0; index < count; index++) {
        const Program *program = &programs[index];
        long long scale = program->scale;
        if (quick && scale > 0) scale = scale / 100 > 0 ? scale / 100 : 1;
        double seconds[MAXIMUM_RUNS];
        long long peak = 0;
        bool ok = true;
        for (int run = 0; run < runs && ok; run++) {
            Measure measure = run_once(novelo, program, scale);
            ok = measure.passed;
            seconds[run] = measure.seconds;
            if (measure.peak_bytes > peak) peak = measure.peak_bytes;
        }
        if (!ok) {
            printf("  FAIL  %s\n", program->name);
            continue;
        }
        passed++;
        if (quick) {
            printf("  ok    %-12s scale %lld\n", program->name, scale);
            continue;
        }
        qsort(seconds, (size_t)runs, sizeof(double), compare_doubles);
        double median = runs % 2 ? seconds[runs / 2] : (seconds[runs / 2 - 1] + seconds[runs / 2]) / 2.0;
        double spread = seconds[runs - 1] - seconds[0];
        printf("  %-12s %10lld %10.3f %10.3f %9.1f", program->name, scale, median, spread, megabytes(peak));
        const BaselineEntry *entry = find_entry(baseline, baseline_count, program->name);
        if (entry && entry->seconds > 0) {
            printf(" %10.3f %+7.1f%%", entry->seconds, (median / entry->seconds - 1.0) * 100.0);
        }
        putchar('\n');
        if (written) fprintf(written, "%-12s %.3f %.1f\n", program->name, median, megabytes(peak));
    }
    printf("bench: %zu passed, %zu failed\n", passed, count - passed);
    if (written) fclose(written);
    free(baseline);
    free(programs);
    return passed == count ? 0 : 1;
}
