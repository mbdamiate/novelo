/* Fuzzer: fuzz [--iterations N] [--seed S] [--only MODE:INDEX] SOURCES…
 *
 * Mutates known-good programs and checks that the interpreter never crashes,
 * never trips a sanitizer and never leaks, whatever it is given. Built with
 * AddressSanitizer + UBSan by `make fuzz`. Neither Apple clang nor gcc ships
 * libFuzzer, so this is a plain, deterministic mutation fuzzer: input INDEX of
 * a mode depends only on the seed and INDEX, and --only reproduces it.
 *
 * SOURCES are conformance files (the `program` and `files` of every eval case,
 * the `input` of every read case) and directories of examples (each
 * NN-name/main.nvl, with its .nvl files and input.txt).
 *
 * Two modes:
 *
 *   eval    N inputs, one process each, evaluated as a program with the seed's
 *           files and standard input. The program may succeed, fail to read or
 *           raise an uncaught error; anything else is a failure: a signal, a
 *           sanitizer report, a descriptor without a :kind, a leaked object or
 *           position entry. A run over TIME_LIMIT_SECONDS is counted, not failed:
 *           a mutated loop bound easily makes a loop that never ends. Inputs
 *           that do not read are drawn again, so most of them expand and run.
 *   reader  10 × N inputs through lisp_read alone, in batches per process. A
 *           read error must have a position inside the input; datums must
 *           print as text that reads back equal? and prints the same again.
 *
 * There is no oracle for values: a wrong result that stays inside its memory
 * goes unseen, which the conformance cases are for.
 *
 * Each failing input is saved as build/fuzz/crashes/MODE-INDEX.nvl, with the
 * process's standard error in MODE-INDEX.log.
 */
/* sysconf(_SC_NPROCESSORS_ONLN) is not POSIX: glibc hides it under the
 * strict _POSIX_C_SOURCE of the Makefile. */
#define _DEFAULT_SOURCE

#include "buffer.h"
#include "error.h"
#include "io.h"
#include "json.h"
#include "positions.h"
#include "printer.h"
#include "program.h"
#include "reader.h"
#include "value.h"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

constexpr unsigned TIME_LIMIT_SECONDS = 2;
constexpr size_t MAXIMUM_INPUT = 1 << 16;
constexpr size_t READER_BATCH = 500;
constexpr int READER_FACTOR = 10;
/* Evaluation runs on a 512 MB stack, like the command line, but may use only
 * this much of it: deep recursion stops early, and a few runs side by side
 * stay within memory. */
constexpr size_t STACK_LIMIT = 32u << 20;
constexpr int MAXIMUM_WORKERS = 8;

/* Exit codes of a child that finished normally. */
enum {
    CHILD_OK = 10,
    CHILD_READ_ERROR = 11,
    CHILD_UNCAUGHT_ERROR = 12,
};

static const char *crash_directory = "build/fuzz/crashes";
static const char *work_directory = "build/fuzz/work";

/* ---- bytes and randomness ----------------------------------------------------------- */

typedef struct Bytes {
    char *data;
    size_t length;
    size_t capacity;
} Bytes;

static void bytes_reserve(Bytes *bytes, size_t extra) {
    if (bytes->length + extra + 1 <= bytes->capacity) return;
    size_t capacity = bytes->capacity ? bytes->capacity : 64;
    while (capacity < bytes->length + extra + 1) capacity *= 2;
    bytes->data = realloc(bytes->data, capacity);
    bytes->capacity = capacity;
}

static void bytes_set(Bytes *bytes, const char *text, size_t length) {
    bytes->length = 0;
    bytes_reserve(bytes, length);
    if (length) memcpy(bytes->data, text, length);
    bytes->length = length;
    bytes->data[length] = '\0';
}

/* Replaces `removed` bytes at `at` with `inserted`. */
static void bytes_splice(Bytes *bytes, size_t at, size_t removed, const char *inserted, size_t inserted_length) {
    if (at > bytes->length) at = bytes->length;
    if (removed > bytes->length - at) removed = bytes->length - at;
    bytes_reserve(bytes, inserted_length);
    memmove(bytes->data + at + inserted_length, bytes->data + at + removed, bytes->length - at - removed);
    if (inserted_length) memcpy(bytes->data + at, inserted, inserted_length);
    bytes->length = bytes->length - removed + inserted_length;
    bytes->data[bytes->length] = '\0';
}

/* splitmix64: small, fast and good enough to pick mutations. */
typedef struct Random {
    uint64_t state;
} Random;

static uint64_t random_next(Random *random) {
    uint64_t value = (random->state += 0x9E3779B97F4A7C15ULL);
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

/* A number in [0, bound); bound must be positive. */
static size_t random_below(Random *random, size_t bound) { return (size_t)(random_next(random) % bound); }

/* ---- seeds -------------------------------------------------------------------------- */

typedef struct Seed {
    char *name;            /* where it came from, for the report */
    char *text;            /* the program, or the reader input */
    size_t length;
    char *input;           /* standard input */
    size_t input_length;
    LispMemoryFile *files; /* the files beside main.nvl, paths starting with '/' */
    size_t file_count;
    bool is_program;       /* false for the input of a read case: reader mode only */
} Seed;

typedef struct Corpus {
    Seed *seeds;
    size_t count, capacity;
    size_t *programs;      /* indexes of the seeds that are programs */
    size_t program_count;
    char **tokens;         /* distinct tokens of every seed, for splicing */
    size_t token_count, token_capacity;
    char **callees;        /* distinct tokens right after a '(': mostly names of functions and forms */
    size_t callee_count, callee_capacity;
} Corpus;

static char *copy_bytes(const char *text, size_t length) {
    char *copy = malloc(length + 1);
    if (length) memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static Seed *add_seed(Corpus *corpus, const char *name, const char *text, size_t length, bool is_program) {
    if (corpus->count == corpus->capacity) {
        corpus->capacity = corpus->capacity ? corpus->capacity * 2 : 256;
        corpus->seeds = realloc(corpus->seeds, corpus->capacity * sizeof(Seed));
    }
    Seed *seed = &corpus->seeds[corpus->count++];
    *seed = (Seed){.name = strdup(name), .text = copy_bytes(text, length), .length = length, .is_program = is_program};
    seed->input = copy_bytes("", 0);
    return seed;
}

static void add_file(Seed *seed, const char *path, const char *text, size_t length) {
    seed->files = realloc(seed->files, (seed->file_count + 1) * sizeof(LispMemoryFile));
    size_t path_length = strlen(path) + 2;
    char *absolute = malloc(path_length);
    snprintf(absolute, path_length, "/%s", path);
    seed->files[seed->file_count++] = (LispMemoryFile){.path = absolute, .text = copy_bytes(text, length), .length = length};
}

static char *read_whole_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (!file) return nullptr;
    LispBuffer buffer;
    lisp_buffer_init(&buffer);
    char chunk[65536];
    size_t chunk_length;
    while ((chunk_length = fread(chunk, 1, sizeof chunk, file)) > 0) lisp_buffer_append(&buffer, chunk, chunk_length);
    fclose(file);
    char *text = lisp_buffer_take(&buffer, length);
    return text ? text : copy_bytes("", *length = 0);
}

static const Json *string_of(const Json *object, const char *name) {
    const Json *member = json_member(object, name);
    return member && member->kind == JSON_STRING ? member : nullptr;
}

static void load_conformance(Corpus *corpus, const char *path) {
    size_t length;
    char *text = read_whole_file(path, &length);
    if (!text) {
        fprintf(stderr, "fuzz: cannot read %s\n", path);
        exit(2);
    }
    Json document;
    char message[256];
    if (!json_parse(text, length, &document, message, sizeof message)) {
        fprintf(stderr, "fuzz: %s: %s\n", path, message);
        exit(2);
    }
    const Json *cases = json_member(&document, "cases");
    for (size_t index = 0; cases && index < cases->count; index++) {
        const Json *test_case = &cases->items[index];
        const Json *kind = string_of(test_case, "kind");
        const Json *identifier = string_of(test_case, "id");
        const char *name = identifier ? identifier->string : "?";
        if (!kind) continue;
        if (strcmp(kind->string, "eval") == 0) {
            const Json *program = string_of(test_case, "program");
            if (!program) continue;
            Seed *seed = add_seed(corpus, name, program->string, program->string_length, true);
            const Json *input = string_of(test_case, "input");
            if (input) {
                free(seed->input);
                seed->input = copy_bytes(input->string, input->string_length);
                seed->input_length = input->string_length;
            }
            const Json *files = json_member(test_case, "files");
            for (size_t file = 0; files && file < files->count; file++) {
                if (files->items[file].kind != JSON_STRING) continue;
                add_file(seed, files->keys[file], files->items[file].string, files->items[file].string_length);
            }
        } else {
            const Json *input = string_of(test_case, "input");
            if (input) add_seed(corpus, name, input->string, input->string_length, false);
        }
    }
    json_free(&document);
    free(text);
}

static bool ends_with(const char *name, const char *suffix) {
    size_t name_length = strlen(name), suffix_length = strlen(suffix);
    return name_length >= suffix_length && strcmp(name + name_length - suffix_length, suffix) == 0;
}

/* Each subdirectory with a main.nvl is one seed; its other .nvl files are
 * the files beside it, and input.txt its standard input. */
static void load_examples(Corpus *corpus, const char *directory) {
    DIR *listing = opendir(directory);
    if (!listing) {
        fprintf(stderr, "fuzz: cannot open %s\n", directory);
        exit(2);
    }
    char **names = nullptr;
    size_t name_count = 0;
    for (struct dirent *entry; (entry = readdir(listing));) {
        if (entry->d_name[0] == '.') continue;
        names = realloc(names, (name_count + 1) * sizeof(char *));
        names[name_count++] = strdup(entry->d_name);
    }
    closedir(listing);
    for (size_t index = 0; index < name_count; index++) {
        char path[4096];
        snprintf(path, sizeof path, "%s/%s/main.nvl", directory, names[index]);
        size_t length;
        char *text = read_whole_file(path, &length);
        if (text) {
            Seed *seed = add_seed(corpus, names[index], text, length, true);
            free(text);
            snprintf(path, sizeof path, "%s/%s/input.txt", directory, names[index]);
            char *input = read_whole_file(path, &length);
            if (input) {
                free(seed->input);
                seed->input = input;
                seed->input_length = length;
            }
            snprintf(path, sizeof path, "%s/%s", directory, names[index]);
            DIR *files = opendir(path);
            for (struct dirent *entry; files && (entry = readdir(files));) {
                if (!ends_with(entry->d_name, ".nvl") || strcmp(entry->d_name, "main.nvl") == 0) continue;
                char file_path[sizeof path + sizeof entry->d_name + 1]; /* gcc checks it can never truncate */
                snprintf(file_path, sizeof file_path, "%s/%s", path, entry->d_name);
                char *file_text = read_whole_file(file_path, &length);
                if (file_text) add_file(seed, entry->d_name, file_text, length);
                free(file_text);
            }
            if (files) closedir(files);
        }
        free(names[index]);
    }
    free(names);
}

static bool is_delimiter(char character) {
    return character == ' ' || character == '\n' || character == '\t' || character == '\r' || character == '('
           || character == ')' || character == '[' || character == ']' || character == '{' || character == '}'
           || character == '"' || character == ';';
}

static int compare_strings(const void *left, const void *right) {
    return strcmp(*(char *const *)left, *(char *const *)right);
}

static void add_token(char ***tokens, size_t *count, size_t *capacity, const char *text, size_t length) {
    if (*count == *capacity) {
        *capacity = *capacity ? *capacity * 2 : 1024;
        *tokens = realloc(*tokens, *capacity * sizeof(char *));
    }
    (*tokens)[(*count)++] = copy_bytes(text, length);
}

/* Sorts and removes repeats, so that the pool, and so every input, does not
 * depend on the order of the sources. */
static void make_distinct(char **tokens, size_t *count) {
    qsort(tokens, *count, sizeof(char *), compare_strings);
    size_t distinct = 0;
    for (size_t index = 0; index < *count; index++) {
        if (distinct > 0 && strcmp(tokens[distinct - 1], tokens[index]) == 0) {
            free(tokens[index]);
        } else {
            tokens[distinct++] = tokens[index];
        }
    }
    *count = distinct;
}

/* Skips delimiters, comments and string literals, whose words are rarely
 * names a program defines. */
static size_t skip_to_token(const char *text, size_t length, size_t position) {
    while (position < length) {
        if (text[position] == ';') {
            while (position < length && text[position] != '\n') position++;
        } else if (text[position] == '"') {
            position++;
            while (position < length && text[position] != '"') position += text[position] == '\\' ? 2 : 1;
            position++;
        } else if (is_delimiter(text[position])) {
            position++;
        } else {
            break;
        }
    }
    return position < length ? position : length;
}

/* The runs of non-delimiter bytes of every seed's code, and apart those
 * right after a '('. */
static void collect_tokens(Corpus *corpus) {
    for (size_t index = 0; index < corpus->count; index++) {
        const Seed *seed = &corpus->seeds[index];
        size_t position = 0;
        while (position < seed->length) {
            position = skip_to_token(seed->text, seed->length, position);
            size_t start = position;
            while (position < seed->length && !is_delimiter(seed->text[position])) position++;
            size_t length = position - start;
            if (length == 0 || length > 40 || memchr(seed->text + start, '\0', length)) continue;
            add_token(&corpus->tokens, &corpus->token_count, &corpus->token_capacity, seed->text + start, length);
            if (start > 0 && seed->text[start - 1] == '(') {
                add_token(&corpus->callees, &corpus->callee_count, &corpus->callee_capacity, seed->text + start, length);
            }
        }
    }
    make_distinct(corpus->tokens, &corpus->token_count);
    make_distinct(corpus->callees, &corpus->callee_count);
}

/* ---- mutations ---------------------------------------------------------------------- */

static const char *const interesting_bytes = "()[]{}\"\\#:,`'@ \n;.-+0123456789eE";

static const char *const edge_numbers[] = {
    "9223372036854775807", "-9223372036854775808", "9223372036854775808", "-9223372036854775809",
    "0", "-0", "-0.0", "0.0", "1.7976931348623157e308", "1.7976931348623159e308", "5.0e-324", "2.0e-324",
    "4.9e-324", "0.5", "-1", "1e400", "1.0e-400", "1.", ".5", "+1", "1e", "0x10", "1_000",
};

static const char *const awkward_fragments[] = {
    "\xc3", "\xc3\x28", "\xe2\x82", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xc0\xaf", "\xff", "\xfe\xff",
    "\"\\u{110000}\"", "\"\\u{D800}\"", "\"\\u{", "\"\\u{41}\"", "\"\\u{0}\"", "\"\\q\"", "\"\\", "#\\",
    "#{", "#{}", "{}", "#void", "#true", "#false", "#(", "#x", ":", "::", "'", "`", ",", ",@", "&rest",
    "otherwise", "(quote)", "(lambda)", "(define)", "(if)", "(let)", "(match)", "(try)", "(catch)", "(raise)",
    "\"\xe2\x82\xac\"", "\xc2\xa0", "\xe3\x81\x82", "(import io)", "(import list)", "(import text)",
    "(include \"main.nvl\")", "(include \"missing.nvl\")",
};

static size_t token_start(const Bytes *bytes, size_t position) {
    while (position > 0 && !is_delimiter(bytes->data[position - 1])) position--;
    return position;
}

static size_t token_end(const Bytes *bytes, size_t position) {
    while (position < bytes->length && !is_delimiter(bytes->data[position])) position++;
    return position;
}

/* A random position holding one of the bytes in `set`, or SIZE_MAX. */
static size_t find_any(Random *random, const Bytes *bytes, const char *set) {
    if (bytes->length == 0) return SIZE_MAX;
    size_t start = random_below(random, bytes->length);
    for (size_t step = 0; step < bytes->length; step++) {
        size_t position = (start + step) % bytes->length;
        if (bytes->data[position] && strchr(set, bytes->data[position])) return position;
    }
    return SIZE_MAX;
}

typedef enum Mode {
    MODE_EVAL,
    MODE_READER,
} Mode;

/* Most byte-level mutations stop a program from reading at all, which the
 * reader mode covers already; an eval input mostly swaps tokens and numbers
 * and wraps stretches in forms, so it goes on to expand and run. */
static const int eval_mutations[] = {10, 10, 10, 10, 10, 10, 5, 5, 5, 6, 6, 9, 9, 7, 8, 0, 1, 2, 3, 4};

/* Values of every type, to put where a program passes an argument. */
static const char *const literals[] = {
    "0", "1", "-1", "3", "100", "9223372036854775807", "-9223372036854775808", "1.5", "-0.0", "0.0",
    "1.7976931348623157e308", "5.0e-324", "\"\"", "\"abc\"", "\"h\\u{E9}llo\"", "\"\\u{1F600}\"", ":k", ":kind",
    "'x", "'()", "'(1 2 3)", "(list 1 \"a\" :b)", "{}", "{:a 1 :b 2}", "{:kind :custom}", "#{}", "#{1 2}",
    "#void", "#true", "#false", "(lambda (x) x)", "(lambda xs xs)", "first", "+",
};

static void mutate_once(Random *random, const Corpus *corpus, Bytes *bytes, Mode mode) {
    size_t at = random_below(random, bytes->length + 1);
    size_t choice = mode == MODE_EVAL ? (size_t)eval_mutations[random_below(random, sizeof eval_mutations / sizeof *eval_mutations)]
                                      : random_below(random, 10);
    if (choice == 10) { /* replace an argument, a token that is not called, with a value of any type */
        size_t start = token_start(bytes, skip_to_token(bytes->data, bytes->length, at));
        size_t end = token_end(bytes, start);
        if (end > start && (start == 0 || bytes->data[start - 1] != '(')) {
            const char *literal = literals[random_below(random, sizeof literals / sizeof *literals)];
            bytes_splice(bytes, start, end - start, literal, strlen(literal));
        }
        return;
    }
    switch (choice) {
    case 0: /* flip a bit */
        if (bytes->length) bytes->data[random_below(random, bytes->length)] ^= (char)(1 << random_below(random, 8));
        break;
    case 1: { /* insert a byte, often one that means something */
        char byte = random_below(random, 3) ? interesting_bytes[random_below(random, strlen(interesting_bytes))]
                                            : (char)random_below(random, 256);
        bytes_splice(bytes, at, 0, &byte, 1);
        break;
    }
    case 2: /* delete a few bytes */
        bytes_splice(bytes, at, 1 + random_below(random, 8), "", 0);
        break;
    case 3: { /* delete a delimiter */
        size_t position = find_any(random, bytes, "()[]{}\"");
        if (position != SIZE_MAX) bytes_splice(bytes, position, 1, "", 0);
        break;
    }
    case 4: { /* duplicate a delimiter */
        size_t position = find_any(random, bytes, "()[]{}\"");
        if (position != SIZE_MAX) {
            char delimiter = bytes->data[position]; /* the splice may move the data */
            bytes_splice(bytes, position, 0, &delimiter, 1);
        }
        break;
    }
    case 5: { /* replace a token: with another of the same input, whose names are bound, or with one of
               * another seed, often a name that is called */
        size_t choice = random_below(random, 3);
        char *own = nullptr;
        if (choice == 0 && bytes->length > 0) {
            size_t from = skip_to_token(bytes->data, bytes->length, random_below(random, bytes->length));
            size_t own_end = token_end(bytes, from);
            if (own_end > from) own = copy_bytes(bytes->data + from, own_end - from);
        }
        const char *token = own                        ? own
                            : random_below(random, 2) ? corpus->callees[random_below(random, corpus->callee_count)]
                                                      : corpus->tokens[random_below(random, corpus->token_count)];
        size_t start = token_start(bytes, at), end = token_end(bytes, at);
        if (start == end) {
            bytes_splice(bytes, at, 0, " ", 1);
            bytes_splice(bytes, at + 1, 0, token, strlen(token));
        } else {
            bytes_splice(bytes, start, end - start, token, strlen(token));
        }
        free(own);
        break;
    }
    case 6: { /* replace a number with an edge value */
        const char *number = edge_numbers[random_below(random, sizeof edge_numbers / sizeof *edge_numbers)];
        size_t position = find_any(random, bytes, "0123456789");
        size_t start = position == SIZE_MAX ? 0 : token_start(bytes, position);
        bool is_number = position != SIZE_MAX
                         && (strchr("0123456789", bytes->data[start])
                             || (strchr("+-", bytes->data[start]) && start + 1 < bytes->length
                                 && strchr("0123456789", bytes->data[start + 1])));
        if (is_number) {
            bytes_splice(bytes, start, token_end(bytes, start) - start, number, strlen(number));
        } else {
            size_t before = token_start(bytes, at); /* between tokens, never inside one */
            bytes_splice(bytes, before, 0, " ", 1);
            bytes_splice(bytes, before, 0, number, strlen(number));
        }
        break;
    }
    case 7: { /* insert a fragment: invalid UTF-8, an escape, a bare form */
        const char *fragment = awkward_fragments[random_below(random, sizeof awkward_fragments / sizeof *awkward_fragments)];
        bytes_splice(bytes, at, 0, fragment, strlen(fragment));
        break;
    }
    case 8: { /* copy a stretch of the input somewhere else */
        if (bytes->length == 0) break;
        size_t from = random_below(random, bytes->length);
        size_t length = 1 + random_below(random, bytes->length - from < 64 ? bytes->length - from : 64);
        char *stretch = copy_bytes(bytes->data + from, length);
        bytes_splice(bytes, at, 0, stretch, length);
        free(stretch);
        break;
    }
    default: { /* wrap a stretch in a call to a token, or in a collection */
        static const char *const openers[] = {"(", "[", "{", "#{", "'(", "`(", ",(", "(list "};
        static const char *const closers[] = {")", "]", "}", "}", ")", ")", ")", ")"};
        size_t kind = random_below(random, sizeof openers / sizeof *openers);
        size_t end = at + random_below(random, bytes->length - at + 1);
        bytes_splice(bytes, end, 0, closers[kind], strlen(closers[kind]));
        bytes_splice(bytes, at, 0, openers[kind], strlen(openers[kind]));
        break;
    }
    }
}

static bool reads(const Bytes *bytes) {
    Value *datums = nullptr;
    LispReadError error;
    bool read = lisp_read(bytes->data, bytes->length, &datums, &error);
    lisp_release(datums);
    return read;
}

static const char *mode_name(Mode mode) { return mode == MODE_EVAL ? "eval" : "reader"; }

/* Input INDEX of a mode: a seed and one to four (eval) or eight (reader) mutations, all drawn from
 * a generator seeded by (seed, mode, index) alone. Returns the seed. */
static const Seed *make_input(const Corpus *corpus, uint64_t seed_number, Mode mode, size_t index, Bytes *bytes) {
    Random random = {seed_number * 0x100000001B3ULL ^ ((uint64_t)mode << 56) ^ (uint64_t)index};
    random_next(&random);
    const Seed *seed = mode == MODE_EVAL ? &corpus->seeds[corpus->programs[random_below(&random, corpus->program_count)]]
                                         : &corpus->seeds[random_below(&random, corpus->count)];
    bytes_set(bytes, seed->text, seed->length);
    /* A reader input is now and then random bytes of its own. */
    if (mode == MODE_READER && random_below(&random, 20) == 0) {
        bytes->length = 0;
        size_t length = random_below(&random, 64);
        for (size_t position = 0; position < length; position++) {
            char byte = random_below(&random, 2) ? interesting_bytes[random_below(&random, strlen(interesting_bytes))]
                                                 : (char)random_below(&random, 256);
            bytes_splice(bytes, bytes->length, 0, &byte, 1);
        }
    }
    /* An eval input that does not read is drawn again, up to a few times:
     * the reader mode already covers inputs that do not read. */
    for (int attempt = 0; attempt < 8; attempt++) {
        if (attempt > 0) bytes_set(bytes, seed->text, seed->length);
        size_t mutations = 1 + random_below(&random, mode == MODE_EVAL ? 4 : 8);
        for (size_t count = 0; count < mutations && bytes->length < MAXIMUM_INPUT; count++) {
            mutate_once(&random, corpus, bytes, mode);
        }
        if (mode == MODE_READER || reads(bytes)) break;
    }
    return seed;
}

/* ---- checks, in the child ----------------------------------------------------------- */

static void fail(const char *format, const char *detail) {
    fprintf(stderr, "fuzz: ");
    fprintf(stderr, format, detail);
    fputc('\n', stderr);
    abort();
}

static void check_no_leak(void) {
    if (lisp_live_objects() != 0 || lisp_position_count() != 0) {
        fprintf(stderr, "fuzz: leak: %zu live objects and %zu position entries\n", lisp_live_objects(),
                lisp_position_count());
        abort();
    }
}

/* core §7.2 — a read error is at a line and column of the input: at most one
 * past its last line, and one past the bytes of that line (columns count code
 * points, never more than bytes). */
static void check_read_error(const char *text, size_t length, const LispReadError *error) {
    if (error->code > LISP_READ_NESTING_TOO_DEEP) fail("read error with an unknown code%s", "");
    if (error->position.line < 1 || error->position.column < 1) fail("read error at line or column 0%s", "");
    size_t line = 1, line_start = 0;
    for (size_t position = 0; position < length && line < error->position.line; position++) {
        if (text[position] == '\n') {
            line++;
            line_start = position + 1;
        }
    }
    if (line != error->position.line) fail("read error after the last line%s", "");
    size_t line_end = line_start;
    while (line_end < length && text[line_end] != '\n') line_end++;
    if (error->position.column > line_end - line_start + 1) fail("read error after the end of its line%s", "");
}

typedef struct EvalJob {
    const Seed *seed;
    const Bytes *program;
    bool verbose; /* print the outcome on standard error: --only */
    int exit_code;
} EvalJob;

static void run_eval_on_thread(void *argument) {
    EvalJob *job = argument;
    const Seed *seed = job->seed;
    LispMemoryFile *files = calloc(seed->file_count + 1, sizeof(LispMemoryFile));
    files[0] = (LispMemoryFile){.path = "/main.nvl", .text = job->program->data, .length = job->program->length};
    for (size_t index = 0; index < seed->file_count; index++) files[index + 1] = seed->files[index];
    LispMemoryFiles file_system = {.files = files, .count = seed->file_count + 1};
    LispTextInput input = {.text = seed->input, .length = seed->input_length};

    LispBuffer output;
    lisp_buffer_init(&output);
    LispStateOptions options = {
        .output = lisp_output_to_buffer(&output),
        .input = lisp_input_from_text(&input),
        .files = lisp_file_system_from_memory(&file_system),
        .working_directory = "/",
        .stack_limit = STACK_LIMIT,
    };
    LispState *state = lisp_state_new(&options);
    Value *last_value = nullptr;
    LispReadError read_error;
    LispRunResult result =
        lisp_run_source(state, job->program->data, job->program->length, "main.nvl", &last_value, &read_error);

    LispBuffer text;
    lisp_buffer_init(&text);
    if (result == LISP_RUN_READ_ERROR) {
        check_read_error(job->program->data, job->program->length, &read_error);
        lisp_describe_read_error(&text, "main.nvl", &read_error);
        job->exit_code = CHILD_READ_ERROR;
    } else if (result == LISP_RUN_ERROR) {
        /* core §8.5 — an uncaught error is a Map with a Keyword :kind */
        if (!state->error || state->error->kind != LISP_KIND_MAP) fail("uncaught error that is not a Map%s", "");
        Value *kind = lisp_error_kind(state);
        if (!kind || kind->kind != LISP_KIND_KEYWORD) fail("uncaught error without a Keyword :kind%s", "");
        lisp_describe_error(&text, state->error);
        job->exit_code = CHILD_UNCAUGHT_ERROR;
    } else {
        if (!last_value) fail("finished without a value%s", "");
        lisp_print(&text, last_value, LISP_PRINT_WRITE);
        lisp_print(&text, last_value, LISP_PRINT_DISPLAY);
        job->exit_code = CHILD_OK;
    }
    if (job->verbose) fprintf(stderr, "%s\n", text.data ? text.data : "");
    lisp_buffer_free(&text);
    lisp_release(last_value);
    lisp_state_free(state);
    lisp_buffer_free(&output);
    free(files);
    check_no_leak();
}

/* Every datum prints as text that reads back equal?, and that text prints the
 * same again (core §10). */
static void check_reader_input(const char *text, size_t length) {
    Value *datums = nullptr;
    LispReadError error;
    if (!lisp_read(text, length, &datums, &error)) {
        check_read_error(text, length, &error);
        check_no_leak();
        return;
    }
    for (const Value *list = datums; list != LISP_EMPTY; list = lisp_rest(list)) {
        LispBuffer printed;
        lisp_buffer_init(&printed);
        lisp_print(&printed, lisp_first(list), LISP_PRINT_WRITE);
        Value *again = nullptr;
        LispReadError again_error;
        if (!lisp_read(printed.data, printed.length, &again, &again_error)) {
            fail("the printed datum does not read back: %s", printed.data);
        }
        if (again == LISP_EMPTY || lisp_rest(again) != LISP_EMPTY) {
            fail("the printed datum reads back as other than one datum: %s", printed.data);
        }
        if (!lisp_equal(lisp_first(list), lisp_first(again))) {
            fail("the printed datum reads back different: %s", printed.data);
        }
        LispBuffer reprinted;
        lisp_buffer_init(&reprinted);
        lisp_print(&reprinted, lisp_first(again), LISP_PRINT_WRITE);
        if (reprinted.length != printed.length || memcmp(reprinted.data, printed.data, printed.length) != 0) {
            fail("the datum read back prints differently: %s", printed.data);
        }
        lisp_buffer_free(&reprinted);
        lisp_buffer_free(&printed);
        lisp_release(again);
    }
    lisp_release(datums);
    check_no_leak();
}

/* ---- running children, from the parent ---------------------------------------------- */

typedef struct Totals {
    size_t ok, read_errors, uncaught_errors, timeouts, failures;
} Totals;

static void log_path(char *path, size_t size, int slot) { snprintf(path, size, "%s/slot-%d.log", work_directory, slot); }

/* Forks a child whose standard error goes to the slot's log. */
static pid_t start_child(int slot) {
    fflush(stdout);
    fflush(stderr);
    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        exit(2);
    }
    if (child == 0) {
        char path[4096];
        log_path(path, sizeof path, slot);
        int log = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log >= 0) {
            dup2(log, STDERR_FILENO);
            close(log);
        }
        int null = open("/dev/null", O_RDONLY);
        if (null >= 0) {
            dup2(null, STDIN_FILENO);
            close(null);
        }
    }
    return child;
}

/* A bug that most inputs reach would fail thousands of them: only the first
 * ones are saved and shown. */
constexpr size_t MAXIMUM_SAVED_FAILURES = 20;

static void save_failure(Mode mode, size_t index, const Bytes *input, int slot, int status) {
    static size_t saved = 0;
    if (saved++ >= MAXIMUM_SAVED_FAILURES) {
        if (saved == MAXIMUM_SAVED_FAILURES + 1) printf("  …     more failures are counted, not saved\n");
        return;
    }
    char path[4096];
    snprintf(path, sizeof path, "%s/%s-%zu.nvl", crash_directory, mode_name(mode), index);
    FILE *file = fopen(path, "wb");
    if (file) {
        fwrite(input->data, 1, input->length, file);
        fclose(file);
    }
    char log[4096], saved_log[4096];
    log_path(log, sizeof log, slot);
    snprintf(saved_log, sizeof saved_log, "%s/%s-%zu.log", crash_directory, mode_name(mode), index);
    rename(log, saved_log);
    printf("  FAIL  %s %zu: ", mode_name(mode), index);
    if (WIFSIGNALED(status)) {
        printf("signal %d", WTERMSIG(status));
    } else {
        printf("exit code %d", WEXITSTATUS(status));
    }
    printf(", input in %s, standard error in %s\n", path, saved_log);
    /* the first lines of the report */
    FILE *report = fopen(saved_log, "r");
    char line[512];
    for (int count = 0; report && count < 6 && fgets(line, sizeof line, report); count++) printf("        %s", line);
    if (report) fclose(report);
}

/* Child: evaluate one input and exit with its CHILD_ code. */
static void eval_child(const Corpus *corpus, uint64_t seed_number, size_t index) {
    Bytes program = {};
    const Seed *seed = make_input(corpus, seed_number, MODE_EVAL, index, &program);
    alarm(TIME_LIMIT_SECONDS);
    EvalJob job = {.seed = seed, .program = &program};
    if (!lisp_run_on_large_stack(run_eval_on_thread, &job)) _exit(3);
    free(program.data);
    exit(job.exit_code);
}

/* Child: read inputs [first, last) and exit with CHILD_OK. */
static void reader_child(const Corpus *corpus, uint64_t seed_number, size_t first, size_t last) {
    Bytes input = {};
    alarm(TIME_LIMIT_SECONDS * 10);
    for (size_t index = first; index < last; index++) {
        make_input(corpus, seed_number, MODE_READER, index, &input);
        check_reader_input(input.data, input.length);
    }
    free(input.data);
    exit(CHILD_OK);
}

typedef struct Slot {
    pid_t pid;
    size_t first, last; /* the inputs it runs */
} Slot;

static void count_eval(Totals *totals, const Corpus *corpus, uint64_t seed_number, const Slot *slot, int slot_index,
                       int status) {
    if (WIFEXITED(status) && WEXITSTATUS(status) == CHILD_OK) {
        totals->ok++;
    } else if (WIFEXITED(status) && WEXITSTATUS(status) == CHILD_READ_ERROR) {
        totals->read_errors++;
    } else if (WIFEXITED(status) && WEXITSTATUS(status) == CHILD_UNCAUGHT_ERROR) {
        totals->uncaught_errors++;
    } else if (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM) {
        totals->timeouts++;
    } else {
        totals->failures++;
        Bytes input = {};
        make_input(corpus, seed_number, MODE_EVAL, slot->first, &input);
        save_failure(MODE_EVAL, slot->first, &input, slot_index, status);
        free(input.data);
    }
}

/* Runs one reader input in its own process, to find which input of a failed
 * batch fails. */
static void retry_reader_input(Totals *totals, const Corpus *corpus, uint64_t seed_number, size_t index) {
    pid_t child = start_child(0);
    if (child == 0) reader_child(corpus, seed_number, index, index + 1);
    int status = 0;
    waitpid(child, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) == CHILD_OK) return;
    totals->failures++;
    Bytes input = {};
    make_input(corpus, seed_number, MODE_READER, index, &input);
    save_failure(MODE_READER, index, &input, 0, status);
    free(input.data);
}

/* Runs inputs [0, count) of a mode on up to `workers` processes at a time:
 * one input per process in eval mode, a batch per process in reader mode. */
static void run_mode(Totals *totals, const Corpus *corpus, uint64_t seed_number, Mode mode, size_t count, int workers) {
    Slot slots[MAXIMUM_WORKERS] = {};
    size_t next = 0;
    int running = 0;
    size_t batch = mode == MODE_EVAL ? 1 : READER_BATCH;
    size_t failed_batches[64];
    size_t failed_batch_count = 0;
    while (next < count || running > 0) {
        for (int slot = 0; slot < workers && next < count; slot++) {
            if (slots[slot].pid != 0) continue;
            size_t last = next + batch < count ? next + batch : count;
            pid_t child = start_child(slot + 1);
            if (child == 0) {
                if (mode == MODE_EVAL) eval_child(corpus, seed_number, next);
                reader_child(corpus, seed_number, next, last);
            }
            slots[slot] = (Slot){child, next, last};
            next = last;
            running++;
        }
        int status = 0;
        pid_t finished = wait(&status);
        if (finished < 0) break;
        for (int slot = 0; slot < workers; slot++) {
            if (slots[slot].pid != finished) continue;
            if (mode == MODE_EVAL) {
                count_eval(totals, corpus, seed_number, &slots[slot], slot + 1, status);
            } else if (WIFEXITED(status) && WEXITSTATUS(status) == CHILD_OK) {
                totals->ok += slots[slot].last - slots[slot].first;
            } else if (failed_batch_count < sizeof failed_batches / sizeof *failed_batches) {
                failed_batches[failed_batch_count++] = slots[slot].first;
            }
            slots[slot].pid = 0;
            running--;
        }
    }
    for (size_t index = 0; index < failed_batch_count; index++) {
        size_t first = failed_batches[index];
        size_t last = first + batch < count ? first + batch : count;
        size_t before = totals->failures;
        for (size_t input = first; input < last; input++) retry_reader_input(totals, corpus, seed_number, input);
        totals->ok += (last - first) - (totals->failures - before);
    }
}

/* ---- main ----------------------------------------------------------------------------- */

static void print_totals(Mode mode, const Totals *totals) {
    if (mode == MODE_EVAL) {
        printf("fuzz eval: %zu ran (%zu finished, %zu read errors, %zu uncaught errors, %zu timed out), %zu failed\n",
               totals->ok + totals->read_errors + totals->uncaught_errors + totals->timeouts + totals->failures,
               totals->ok, totals->read_errors, totals->uncaught_errors, totals->timeouts, totals->failures);
    } else {
        printf("fuzz reader: %zu passed, %zu failed\n", totals->ok, totals->failures);
    }
}

static void usage(const char *program) {
    fprintf(stderr, "usage: %s [--iterations N] [--seed S] [--only eval:INDEX|reader:INDEX] SOURCES…\n", program);
    exit(2);
}

int main(int argument_count, char **arguments) {
    size_t iterations = 20000;
    uint64_t seed_number = 1;
    const char *only = nullptr;
    Corpus corpus = {};
    for (int index = 1; index < argument_count; index++) {
        const char *argument = arguments[index];
        if (strcmp(argument, "--iterations") == 0 && index + 1 < argument_count) {
            iterations = strtoull(arguments[++index], nullptr, 10);
        } else if (strcmp(argument, "--seed") == 0 && index + 1 < argument_count) {
            seed_number = strtoull(arguments[++index], nullptr, 10);
        } else if (strcmp(argument, "--only") == 0 && index + 1 < argument_count) {
            only = arguments[++index];
        } else if (argument[0] == '-') {
            usage(arguments[0]);
        } else if (ends_with(argument, ".json")) {
            load_conformance(&corpus, argument);
        } else {
            load_examples(&corpus, argument);
        }
    }
    for (size_t index = 0; index < corpus.count; index++) {
        if (!corpus.seeds[index].is_program) continue;
        corpus.programs = realloc(corpus.programs, (corpus.program_count + 1) * sizeof(size_t));
        corpus.programs[corpus.program_count++] = index;
    }
    if (corpus.program_count == 0) usage(arguments[0]);
    collect_tokens(&corpus);

    /* --only MODE:INDEX runs that input here, in this process, with the
     * report on standard error and the input on standard output. */
    if (only) {
        Mode mode = strncmp(only, "eval:", 5) == 0 ? MODE_EVAL : MODE_READER;
        const char *colon = strchr(only, ':');
        if (!colon) usage(arguments[0]);
        size_t index = strtoull(colon + 1, nullptr, 10);
        Bytes input = {};
        const Seed *seed = make_input(&corpus, seed_number, mode, index, &input);
        fprintf(stderr, "%s %zu, from %s:\n", mode_name(mode), index, seed->name);
        fwrite(input.data, 1, input.length, stdout);
        fputc('\n', stdout);
        fflush(stdout);
        if (mode == MODE_EVAL) {
            EvalJob job = {.seed = seed, .program = &input, .verbose = true};
            if (!lisp_run_on_large_stack(run_eval_on_thread, &job)) return 3;
        } else {
            check_reader_input(input.data, input.length);
            fprintf(stderr, "passed\n");
        }
        free(input.data);
        return 0;
    }

    mkdir("build", 0755);
    mkdir("build/fuzz", 0755);
    mkdir(crash_directory, 0755);
    mkdir(work_directory, 0755);
    long processors = sysconf(_SC_NPROCESSORS_ONLN);
    int workers = processors < 1 ? 1 : processors > MAXIMUM_WORKERS ? MAXIMUM_WORKERS : (int)processors;
    printf("fuzz: %zu seeds (%zu programs), %zu tokens (%zu called), seed %llu, %d processes\n", corpus.count,
           corpus.program_count, corpus.token_count, corpus.callee_count, (unsigned long long)seed_number, workers);

    Totals reader = {}, eval = {};
    run_mode(&reader, &corpus, seed_number, MODE_READER, iterations * READER_FACTOR, workers);
    print_totals(MODE_READER, &reader);
    run_mode(&eval, &corpus, seed_number, MODE_EVAL, iterations, workers);
    print_totals(MODE_EVAL, &eval);

    for (size_t index = 0; index < corpus.count; index++) {
        Seed *seed = &corpus.seeds[index];
        for (size_t file = 0; file < seed->file_count; file++) {
            free((char *)seed->files[file].path);
            free((char *)seed->files[file].text);
        }
        free(seed->files);
        free(seed->name);
        free(seed->text);
        free(seed->input);
    }
    for (size_t index = 0; index < corpus.token_count; index++) free(corpus.tokens[index]);
    free(corpus.tokens);
    for (size_t index = 0; index < corpus.callee_count; index++) free(corpus.callees[index]);
    free(corpus.callees);
    free(corpus.programs);
    free(corpus.seeds);
    return reader.failures + eval.failures == 0 ? 0 : 1;
}
