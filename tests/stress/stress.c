/* Stress runner: stress [-k TEXT] [--quick] DIRECTORY
 *
 * Runs every .nvl file of DIRECTORY, each in its own process, and checks that it
 * prints exactly "ok\n" and ends without an uncaught error, and that no object
 * or position entry is left alive once its state is freed. The programs check
 * their own results and raise an error when one is wrong.
 *
 * Comments at the top of a program set how it runs:
 *
 *   ; scale: N             standard input is the line N; the program reads its size from it
 *   ; memory: constant     also run with 10 × N, whose peak memory must stay close to that of N
 *   ; include-chain: K     files chain-1.nvl … chain-K.nvl, each defining chain-I and including the next
 *
 * --quick divides every scale by 100 (at least 1) and skips the memory
 * check: for sanitizer builds, where memory and time are not comparable.
 */
/* wait4, which reports a child's peak memory, is a BSD extension: glibc
 * hides it under the strict _POSIX_C_SOURCE of the Makefile. */
#define _DEFAULT_SOURCE

#include "buffer.h"
#include "error.h"
#include "io.h"
#include "positions.h"
#include "program.h"
#include "value.h"

#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

constexpr unsigned TIME_LIMIT_SECONDS = 600;
/* The run with 10 × N may use this much more memory than the run with N. */
constexpr double MEMORY_GROWTH = 1.25;
constexpr long long MEMORY_SLACK_BYTES = 16LL << 20;

typedef struct Program {
    char *name;
    char *source;
    size_t length;
    long long scale;         /* 0: no scale line on standard input */
    bool constant_memory;
    int include_chain;       /* number of chained files, 0 for none */
} Program;

/* ---- reading the programs ----------------------------------------------------------- */

static char *read_whole_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (!file) return nullptr;
    LispBuffer buffer;
    lisp_buffer_init(&buffer);
    char chunk[65536];
    size_t chunk_length;
    while ((chunk_length = fread(chunk, 1, sizeof chunk, file)) > 0) lisp_buffer_append(&buffer, chunk, chunk_length);
    fclose(file);
    return lisp_buffer_take(&buffer, length);
}

/* Reads the directives from the comment lines at the top. */
static void read_directives(Program *program) {
    const char *line = program->source;
    while (*line == ';') {
        long long number;
        if (sscanf(line, "; scale: %lld", &number) == 1) program->scale = number;
        if (strncmp(line, "; memory: constant", 18) == 0) program->constant_memory = true;
        if (sscanf(line, "; include-chain: %lld", &number) == 1) program->include_chain = (int)number;
        const char *newline = strchr(line, '\n');
        if (!newline) break;
        line = newline + 1;
    }
}

static int compare_names(const void *left, const void *right) {
    return strcmp(((const Program *)left)->name, ((const Program *)right)->name);
}

static size_t load_programs(const char *directory, const char *filter, Program **programs) {
    DIR *listing = opendir(directory);
    if (!listing) {
        fprintf(stderr, "stress: cannot open %s\n", directory);
        exit(2);
    }
    size_t count = 0, capacity = 16;
    *programs = calloc(capacity, sizeof(Program));
    for (struct dirent *entry; (entry = readdir(listing));) {
        size_t name_length = strlen(entry->d_name);
        if (name_length < 5 || strcmp(entry->d_name + name_length - 4, ".nvl") != 0) continue;
        if (filter && !strstr(entry->d_name, filter)) continue;
        if (count == capacity) *programs = realloc(*programs, (capacity *= 2) * sizeof(Program));
        Program *program = &(*programs)[count];
        *program = (Program){.name = strdup(entry->d_name)};
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", directory, entry->d_name);
        program->source = read_whole_file(path, &program->length);
        if (!program->source) {
            fprintf(stderr, "stress: cannot read %s\n", path);
            exit(2);
        }
        read_directives(program);
        count++;
    }
    closedir(listing);
    qsort(*programs, count, sizeof(Program), compare_names);
    return count;
}

/* ---- running one program, in the child ---------------------------------------------- */

typedef struct Run {
    const Program *program;
    long long scale;
    bool passed;
} Run;

static void report(const char *format, const char *detail) {
    fprintf(stderr, format, detail);
    fputc('\n', stderr);
}

static void run_on_thread(void *argument) {
    Run *run = argument;
    const Program *program = run->program;

    char scale_line[32];
    int scale_length = run->scale > 0 ? snprintf(scale_line, sizeof scale_line, "%lld\n", run->scale) : 0;
    LispTextInput input = {.text = scale_line, .length = (size_t)scale_length};

    /* the program at /main.nvl, then the chain of included files */
    int chain = program->include_chain;
    LispMemoryFile *files = calloc((size_t)chain + 1, sizeof(LispMemoryFile));
    char **texts = calloc((size_t)chain + 1, sizeof(char *));
    char **paths = calloc((size_t)chain + 1, sizeof(char *));
    files[0] = (LispMemoryFile){.path = "/main.nvl", .text = program->source, .length = program->length};
    for (int index = 1; index <= chain; index++) {
        paths[index] = malloc(64);
        snprintf(paths[index], 64, "/chain-%d.nvl", index);
        texts[index] = malloc(128);
        int length = index < chain
                         ? snprintf(texts[index], 128, "(define chain-%d %d)\n(include \"chain-%d.nvl\")\n", index,
                                    index, index + 1)
                         : snprintf(texts[index], 128, "(define chain-%d %d)\n", index, index);
        files[index] = (LispMemoryFile){.path = paths[index], .text = texts[index], .length = (size_t)length};
    }
    LispMemoryFiles file_system = {.files = files, .count = (size_t)chain + 1};

    LispBuffer output;
    lisp_buffer_init(&output);
    LispStateOptions options = {
        .output = lisp_output_to_buffer(&output),
        .input = lisp_input_from_text(&input),
        .files = lisp_file_system_from_memory(&file_system),
        .working_directory = "/",
        .stack_limit = lisp_large_stack_limit(),
    };
    LispState *state = lisp_state_new(&options);
    LispReadError read_error;
    LispRunResult result = lisp_run_source(state, program->source, program->length, "main.nvl", nullptr, &read_error);

    run->passed = false;
    if (result == LISP_RUN_READ_ERROR) {
        fprintf(stderr, "    read error %s at %u:%u\n", lisp_read_error_name(read_error.code),
                read_error.position.line, read_error.position.column);
    } else if (result == LISP_RUN_ERROR) {
        LispBuffer message;
        lisp_buffer_init(&message);
        lisp_describe_error(&message, state->error);
        report("    uncaught error %s", message.data);
        lisp_buffer_free(&message);
    } else if (output.length != 3 || memcmp(output.data, "ok\n", 3) != 0) {
        report("    output \"%s\", expected \"ok\\n\"", output.data ? output.data : "");
    } else {
        run->passed = true;
    }
    lisp_state_free(state);
    lisp_buffer_free(&output);
    for (int index = 1; index <= chain; index++) {
        free(paths[index]);
        free(texts[index]);
    }
    free(paths);
    free(texts);
    free(files);

    if (lisp_live_objects() != 0 || lisp_position_count() != 0) {
        fprintf(stderr, "    leak: %zu live objects and %zu position entries after the run\n", lisp_live_objects(),
                lisp_position_count());
        run->passed = false;
    }
}

/* ---- running one program, from the parent ------------------------------------------- */

typedef struct Measure {
    bool passed;
    double seconds;
    long long peak_bytes;
} Measure;

static Measure measure(const Program *program, long long scale) {
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
        alarm(TIME_LIMIT_SECONDS);
        Run run = {.program = program, .scale = scale};
        if (!lisp_run_on_large_stack(run_on_thread, &run)) {
            fputs("    cannot create the interpreter thread\n", stderr);
            exit(1);
        }
        exit(run.passed ? 0 : 1);
    }
    int status = 0;
    struct rusage usage;
    if (wait4(child, &status, 0, &usage) < 0) {
        perror("wait4");
        exit(2);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    Measure result = {
        .passed = WIFEXITED(status) && WEXITSTATUS(status) == 0,
        .seconds = (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) / 1e9,
#ifdef __APPLE__
        .peak_bytes = usage.ru_maxrss, /* bytes on macOS */
#else
        .peak_bytes = usage.ru_maxrss * 1024LL, /* kilobytes on Linux */
#endif
    };
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "    %s with signal %d\n", WTERMSIG(status) == SIGALRM ? "timed out" : "crashed",
                WTERMSIG(status));
    }
    return result;
}

static double megabytes(long long bytes) { return (double)bytes / (1024.0 * 1024.0); }

static bool run_program(const Program *program, bool quick) {
    long long scale = program->scale;
    if (quick && scale > 0) scale = scale / 100 > 0 ? scale / 100 : 1;
    Measure small = measure(program, scale);
    if (!small.passed) {
        printf("  FAIL  %s\n", program->name);
        return false;
    }
    if (!program->constant_memory || quick) {
        printf("  ok    %-28s scale %-10lld %7.2f s %8.1f MB\n", program->name, scale, small.seconds,
               megabytes(small.peak_bytes));
        return true;
    }
    Measure large = measure(program, scale * 10);
    bool constant = (double)large.peak_bytes <= (double)small.peak_bytes * MEMORY_GROWTH + (double)MEMORY_SLACK_BYTES;
    bool passed = large.passed && constant;
    printf("  %s  %-28s scale %-10lld %7.2f s %8.1f MB   × 10: %7.2f s %8.1f MB%s\n", passed ? "ok  " : "FAIL",
           program->name, scale, small.seconds, megabytes(small.peak_bytes), large.seconds,
           megabytes(large.peak_bytes), large.passed && !constant ? "   memory grew" : "");
    return passed;
}

int main(int argument_count, char **arguments) {
    const char *filter = nullptr;
    const char *directory = nullptr;
    bool quick = false;
    for (int index = 1; index < argument_count; index++) {
        if (strcmp(arguments[index], "-k") == 0 && index + 1 < argument_count) {
            filter = arguments[++index];
        } else if (strcmp(arguments[index], "--quick") == 0) {
            quick = true;
        } else if (!directory && arguments[index][0] != '-') {
            directory = arguments[index];
        } else {
            directory = nullptr;
            break;
        }
    }
    if (!directory) {
        fprintf(stderr, "usage: %s [-k TEXT] [--quick] DIRECTORY\n", arguments[0]);
        return 2;
    }

    Program *programs;
    size_t count = load_programs(directory, filter, &programs);
    size_t passed = 0;
    for (size_t index = 0; index < count; index++) passed += run_program(&programs[index], quick);
    printf("stress: %zu passed, %zu failed\n", passed, count - passed);
    for (size_t index = 0; index < count; index++) {
        free(programs[index].name);
        free(programs[index].source);
    }
    free(programs);
    return passed == count ? 0 : 1;
}
