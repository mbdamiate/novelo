/* core §8.8 — include: paths, error values, load once, the disk. */
#include "test.h"

#include "error.h"
#include "eval.h"
#include "include.h"
#include "printer.h"
#include "program.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- paths ------------------------------------------------------------------------ */

static void check_path(const char *file, int line, const char *directory, const char *path, const char *expected) {
    Value *base = lisp_string_cstr(directory);
    Value *resolved = lisp_path_resolve(base, path);
    if (strcmp(lisp_as_string(resolved)->data, expected) != 0) {
        test_fail(file, line, "resolve(%s, %s) = %s, expected %s", directory, path, lisp_as_string(resolved)->data,
                  expected);
    }
    lisp_release(resolved);
    lisp_release(base);
}
#define CHECK_PATH(directory, path, expected) check_path(__FILE__, __LINE__, (directory), (path), (expected))

static void t_path_resolution(void) {
    CHECK_PATH("/a/b", "c.novelo", "/a/b/c.novelo");
    CHECK_PATH("/a/b", "./c.novelo", "/a/b/c.novelo");
    CHECK_PATH("/a/b", "../c.novelo", "/a/c.novelo");
    CHECK_PATH("/a/b", "lib/../c.novelo", "/a/b/c.novelo");
    CHECK_PATH("/a/b", "/x//y/./z.novelo", "/x/y/z.novelo");
    CHECK_PATH("/", "../../c.novelo", "/c.novelo");
    CHECK_PATH("/a", "..", "/");
    CHECK_PATH("/", "", "/");
}

/* ---- programs over an in-memory file system ---------------------------------------------- */

typedef struct FileRun {
    LispState *state;
    LispBuffer output;
    LispMemoryFiles files;
    LispRunResult result;
    Value *value;
} FileRun;

/* Runs main.novelo at the root, with the given extra files (path, text, …). */
static void file_run(FileRun *run, const char *program, size_t file_count, const LispMemoryFile *files) {
    LispMemoryFile *all = calloc(file_count + 1, sizeof(LispMemoryFile));
    all[0] = (LispMemoryFile){"/main.novelo", program, strlen(program)};
    memcpy(all + 1, files, file_count * sizeof(LispMemoryFile));
    run->files = (LispMemoryFiles){all, file_count + 1};
    lisp_buffer_init(&run->output);
    LispStateOptions options = {
        .output = lisp_output_to_buffer(&run->output),
        .files = lisp_file_system_from_memory(&run->files),
        .working_directory = "/",
    };
    run->state = lisp_state_new(&options);
    run->value = nullptr;
    LispReadError read_error;
    run->result = lisp_run_source(run->state, program, strlen(program), "main.novelo", &run->value, &read_error);
}

static void file_run_finish(FileRun *run) {
    lisp_release(run->value);
    lisp_state_free(run->state);
    lisp_buffer_free(&run->output);
    free((void *)run->files.files);
}

#define FILE(path, text) ((LispMemoryFile){(path), (text), sizeof(text) - 1})

/* The :values of the uncaught error, printed. */
static void check_error_values(const char *file, int line, FileRun *run, const char *expected) {
    if (run->result != LISP_RUN_ERROR) {
        test_fail(file, line, "expected an error");
        return;
    }
    LispBuffer printed;
    lisp_buffer_init(&printed);
    lisp_print(&printed, lisp_map_get(run->state->error, lisp_keyword_cstr("values")), LISP_PRINT_WRITE);
    if (strcmp(printed.data, expected) != 0) test_fail(file, line, ":values %s, expected %s", printed.data, expected);
    lisp_buffer_free(&printed);
}
#define CHECK_ERROR_VALUES(run, expected) check_error_values(__FILE__, __LINE__, (run), (expected))

static void t_error_values(void) {
    FileRun run;
    file_run(&run, "(include \"missing.novelo\")", 0, nullptr);
    CHECK_ERROR_VALUES(&run, "(\"missing.novelo\" :cannot-open)");
    file_run_finish(&run);

    file_run(&run, "(include \"lib/bad.novelo\")", 1, (LispMemoryFile[]){FILE("/lib/bad.novelo", "(define x 1)\n  {:a}")});
    CHECK_ERROR_VALUES(&run, "(\"lib/bad.novelo\" :odd-map 2 3)");
    file_run_finish(&run);

    file_run(&run, "(include \"a.novelo\")", 1, (LispMemoryFile[]){FILE("/a.novelo", "(include \"./a.novelo\" again)")});
    CHECK_ERROR_VALUES(&run, "(\"./a.novelo\" :cycle)");
    file_run_finish(&run);

    file_run(&run, "(include \"lib\")", 0, nullptr); /* not a file of the in-memory system */
    CHECK_ERROR_VALUES(&run, "(\"lib\" :cannot-open)");
    file_run_finish(&run);
}

static void t_nested_directories(void) {
    FileRun run;
    file_run(&run, "(include \"a/b/c.novelo\" c) (c/where)", 3,
             (LispMemoryFile[]){
                 FILE("/a/b/c.novelo", "(include \"../d.novelo\")\n(define where (lambda () (list here there)))"),
                 FILE("/a/d.novelo", "(include \"/e.novelo\")\n(define here :a-d)"),
                 FILE("/e.novelo", "(define there :root-e)"),
             });
    CHECK_INT(run.result, LISP_RUN_OK);
    CHECK_EQUAL(run.value, "(:a-d :root-e)");
    file_run_finish(&run);
}

/* Definitions made before an error in an included file remain (§8.8). */
static void t_definitions_before_error_remain(void) {
    FileRun run;
    file_run(&run, "(include \"lib.novelo\" l)", 1,
             (LispMemoryFile[]){FILE("/lib.novelo", "(define kept 1)\n(error \"stop\")\n(define lost 2)")});
    CHECK_INT(run.result, LISP_RUN_ERROR);
    CHECK(lisp_global_find(run.state, lisp_symbol_cstr("l/kept")) != nullptr);
    CHECK(lisp_global_find(run.state, lisp_symbol_cstr("l/lost")) == nullptr);
    /* the file is no longer "still loading" after the error */
    CHECK(run.state->loading == LISP_EMPTY);
    file_run_finish(&run);
}

/* A file that could not be opened may be included later (as in the REPL). */
static void t_retry_after_failure(void) {
    LispMemoryFile file = FILE("/later.novelo", "(define later :here)");
    LispMemoryFiles empty = {nullptr, 0};
    LispMemoryFiles with_file = {&file, 1};
    LispBuffer output;
    lisp_buffer_init(&output);
    LispStateOptions options = {
        .output = lisp_output_to_buffer(&output),
        .files = lisp_file_system_from_memory(&empty),
        .working_directory = "/",
    };
    LispState *state = lisp_state_new(&options);
    const char *include = "(include \"later.novelo\")";
    LispReadError read_error;
    CHECK_INT(lisp_run_source(state, include, strlen(include), nullptr, nullptr, &read_error), LISP_RUN_ERROR);
    state->files = lisp_file_system_from_memory(&with_file);
    Value *value = nullptr;
    const char *again = "(include \"later.novelo\") later";
    CHECK_INT(lisp_run_source(state, again, strlen(again), nullptr, &value, &read_error), LISP_RUN_OK);
    CHECK_EQUAL(value, ":here");
    lisp_release(value);
    lisp_state_free(state);
    lisp_buffer_free(&output);
}

static void t_disk(void) {
    char directory[] = "/tmp/novelo-include-XXXXXX";
    CHECK(mkdtemp(directory) != nullptr);
    char path[512];
    snprintf(path, sizeof path, "%s/lib.novelo", directory);
    FILE *file = fopen(path, "w");
    CHECK(file != nullptr);
    if (!file) return;
    fputs("(define from-disk :yes)", file);
    fclose(file);

    LispBuffer output;
    lisp_buffer_init(&output);
    LispStateOptions options = {.output = lisp_output_to_buffer(&output), .working_directory = directory};
    LispState *state = lisp_state_new(&options);
    const char *program = "(include \"lib.novelo\" d) d/from-disk";
    Value *value = nullptr;
    LispReadError read_error;
    CHECK_INT(lisp_run_source(state, program, strlen(program), "main.novelo", &value, &read_error), LISP_RUN_OK);
    if (value) CHECK_EQUAL(value, ":yes");
    lisp_release(value);
    lisp_state_free(state);
    lisp_buffer_free(&output);
    unlink(path);
    rmdir(directory);
}

const TestSuite include_suite = {"include", (const TestCase[]){
    {"path resolution", t_path_resolution},
    {"error values", t_error_values},
    {"nested directories and absolute paths", t_nested_directories},
    {"definitions before an error remain", t_definitions_before_error_remain},
    {"a failed include can be retried", t_retry_after_failure},
    {"reads from the disk", t_disk},
    {},
}};
