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
    CHECK_PATH("/a/b", "c.nvl", "/a/b/c.nvl");
    CHECK_PATH("/a/b", "./c.nvl", "/a/b/c.nvl");
    CHECK_PATH("/a/b", "../c.nvl", "/a/c.nvl");
    CHECK_PATH("/a/b", "lib/../c.nvl", "/a/b/c.nvl");
    CHECK_PATH("/a/b", "/x//y/./z.nvl", "/x/y/z.nvl");
    CHECK_PATH("/", "../../c.nvl", "/c.nvl");
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

/* Runs main.nvl at the root, with the given extra files (path, text, …). */
static void file_run(FileRun *run, const char *program, size_t file_count, const LispMemoryFile *files) {
    LispMemoryFile *all = calloc(file_count + 1, sizeof(LispMemoryFile));
    all[0] = (LispMemoryFile){"/main.nvl", program, strlen(program)};
    if (file_count > 0) memcpy(all + 1, files, file_count * sizeof(LispMemoryFile)); /* files may be nullptr */
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
    run->result = lisp_run_source(run->state, program, strlen(program), "main.nvl", &run->value, &read_error);
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
    file_run(&run, "(include \"missing.nvl\")", 0, nullptr);
    CHECK_ERROR_VALUES(&run, "(\"missing.nvl\" :cannot-open)");
    file_run_finish(&run);

    file_run(&run, "(include \"lib/bad.nvl\")", 1, (LispMemoryFile[]){FILE("/lib/bad.nvl", "(define x 1)\n  {:a}")});
    CHECK_ERROR_VALUES(&run, "(\"lib/bad.nvl\" :odd-map 2 3)");
    file_run_finish(&run);

    file_run(&run, "(include \"a.nvl\")", 1, (LispMemoryFile[]){FILE("/a.nvl", "(include \"./a.nvl\" again)")});
    CHECK_ERROR_VALUES(&run, "(\"./a.nvl\" :cycle)");
    file_run_finish(&run);

    file_run(&run, "(include \"lib\")", 0, nullptr); /* not a file of the in-memory system */
    CHECK_ERROR_VALUES(&run, "(\"lib\" :cannot-open)");
    file_run_finish(&run);
}

/* §8.5: :position names the file the failing code was read from. */
static void check_error_position(const char *file, int line, FileRun *run, const char *expected) {
    if (run->result != LISP_RUN_ERROR) {
        test_fail(file, line, "expected an error");
        return;
    }
    Value *position = lisp_map_get(run->state->error, lisp_keyword_cstr("position"));
    if (!position) {
        test_fail(file, line, "the error has no :position, expected %s", expected);
        return;
    }
    test_check_equal(file, line, position, expected);
}
#define CHECK_ERROR_POSITION(run, expected) check_error_position(__FILE__, __LINE__, (run), (expected))

static void t_error_positions(void) {
    FileRun run;
    file_run(&run, "(+ 1\n  (first ()))", 0, nullptr);
    CHECK_ERROR_POSITION(&run, "{:line 2 :column 3 :file \"main.nvl\"}");
    file_run_finish(&run);

    file_run(&run, "(include \"lib/f.nvl\")\n(boom)", 1,
             (LispMemoryFile[]){FILE("/lib/f.nvl", "(define boom (lambda ()\n  (first ())))")});
    CHECK_ERROR_POSITION(&run, "{:line 2 :column 3 :file \"/lib/f.nvl\"}");
    file_run_finish(&run);

    /* an include that fails is at the include form */
    file_run(&run, "\n(include \"missing.nvl\")", 0, nullptr);
    CHECK_ERROR_POSITION(&run, "{:line 2 :column 1 :file \"main.nvl\"}");
    file_run_finish(&run);
}

/* §8.7: the message names the position; a :position the program built is
 * used when it has the right shape and ignored otherwise. */
static void t_uncaught_error_message(void) {
    static const struct {
        const char *program;
        const char *message;
    } cases[] = {
        {"\n  (quotient 1 0)", "main.nvl:2:3: error :division-by-zero: quotient: division by zero"},
        {"(raise {:kind :x :message \"m\" :values (quote (1)) :position {:file \"f\" :line 4 :column 5}})",
         "f:4:5: error :x: m 1"},
        {"(raise {:kind :x :message \"m\" :values () :position {:line 4 :column 5}})", "4:5: error :x: m"},
        {"(raise {:kind :x :message \"m\" :values () :position 5})", "error :x: m"},
    };
    for (size_t index = 0; index < sizeof cases / sizeof cases[0]; index++) {
        FileRun run;
        file_run(&run, cases[index].program, 0, nullptr);
        CHECK_INT(run.result, LISP_RUN_ERROR);
        if (run.result == LISP_RUN_ERROR) {
            LispBuffer message;
            lisp_buffer_init(&message);
            lisp_describe_uncaught_error(&message, run.state->error);
            if (strcmp(message.data, cases[index].message) != 0) {
                test_fail(__FILE__, __LINE__, "message \"%s\", expected \"%s\"", message.data, cases[index].message);
            }
            lisp_buffer_free(&message);
        }
        file_run_finish(&run);
    }
}

static void t_nested_directories(void) {
    FileRun run;
    file_run(&run, "(include \"a/b/c.nvl\" c) (c/where)", 3,
             (LispMemoryFile[]){
                 FILE("/a/b/c.nvl", "(include \"../d.nvl\")\n(define where (lambda () (list here there)))"),
                 FILE("/a/d.nvl", "(include \"/e.nvl\")\n(define here :a-d)"),
                 FILE("/e.nvl", "(define there :root-e)"),
             });
    CHECK_INT(run.result, LISP_RUN_OK);
    CHECK_EQUAL(run.value, "(:a-d :root-e)");
    file_run_finish(&run);
}

/* Definitions made before an error in an included file remain (§8.8). */
static void t_definitions_before_error_remain(void) {
    FileRun run;
    file_run(&run, "(include \"lib.nvl\" l)", 1,
             (LispMemoryFile[]){FILE("/lib.nvl", "(define kept 1)\n(error \"stop\")\n(define lost 2)")});
    CHECK_INT(run.result, LISP_RUN_ERROR);
    CHECK(lisp_global_find(run.state, lisp_symbol_cstr("l/kept")) != nullptr);
    CHECK(lisp_global_find(run.state, lisp_symbol_cstr("l/lost")) == nullptr);
    /* the file is no longer "still loading" after the error */
    CHECK(run.state->loading == LISP_EMPTY);
    file_run_finish(&run);
}

/* A file that could not be opened may be included later (as in the REPL). */
static void t_retry_after_failure(void) {
    LispMemoryFile file = FILE("/later.nvl", "(define later :here)");
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
    const char *include = "(include \"later.nvl\")";
    LispReadError read_error;
    CHECK_INT(lisp_run_source(state, include, strlen(include), nullptr, nullptr, &read_error), LISP_RUN_ERROR);
    state->files = lisp_file_system_from_memory(&with_file);
    Value *value = nullptr;
    const char *again = "(include \"later.nvl\") later";
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
    snprintf(path, sizeof path, "%s/lib.nvl", directory);
    FILE *file = fopen(path, "w");
    CHECK(file != nullptr);
    if (!file) return;
    fputs("(define from-disk :yes)", file);
    fclose(file);

    LispBuffer output;
    lisp_buffer_init(&output);
    LispStateOptions options = {.output = lisp_output_to_buffer(&output), .working_directory = directory};
    LispState *state = lisp_state_new(&options);
    const char *program = "(include \"lib.nvl\" d) d/from-disk";
    Value *value = nullptr;
    LispReadError read_error;
    CHECK_INT(lisp_run_source(state, program, strlen(program), "main.nvl", &value, &read_error), LISP_RUN_OK);
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
    {"error positions name their file", t_error_positions},
    {"the message of an uncaught error", t_uncaught_error_message},
    {"nested directories and absolute paths", t_nested_directories},
    {"definitions before an error remain", t_definitions_before_error_remain},
    {"a failed include can be retried", t_retry_after_failure},
    {"reads from the disk", t_disk},
    {},
}};
