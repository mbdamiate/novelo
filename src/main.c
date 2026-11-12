/* core §8.7 — the command line: `novelo file.nvl` runs a program,
 * `novelo` alone starts the REPL.
 *
 * Exit codes: 0 when every form was evaluated (or the REPL reached the end
 * of its input), 1 for a read error or an uncaught error, 2 for bad
 * arguments or a program file that cannot be read.
 */
#include "error.h"
#include "program.h"
#include "repl.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct Run {
    const char *path; /* nullptr: the REPL */
    int exit_code;
} Run;

/* Messages on stderr, after whatever the program already printed. */
static void write_to_stderr(void *, const char *bytes, size_t length) {
    fflush(stdout);
    fwrite(bytes, 1, length, stderr);
}

static void usage(FILE *stream) {
    fputs("usage: novelo [file.nvl]\n"
          "  with a file, runs the program; without one, starts the REPL.\n"
          "  NOVELO_STDLIB names the standard library document (standard-library.md).\n",
          stream);
}

static void write_error(const LispBuffer *message) {
    fflush(stdout); /* the program's own output comes first */
    fwrite(message->data ? message->data : "", 1, message->length, stderr);
    fputc('\n', stderr);
}

static int run_program(LispState *state, const char *path) {
    LispBuffer source;
    lisp_buffer_init(&source);
    LispFileSystem disk = lisp_file_system_disk();
    if (!disk.read_file(disk.context, path, &source)) {
        fprintf(stderr, "novelo: cannot read %s\n", path);
        lisp_buffer_free(&source);
        return 2;
    }
    LispReadError read_error;
    LispRunResult result =
        lisp_run_source(state, source.data ? source.data : "", source.length, path, nullptr, &read_error);
    lisp_buffer_free(&source);

    LispBuffer message;
    lisp_buffer_init(&message);
    if (result == LISP_RUN_READ_ERROR) {
        lisp_describe_read_error(&message, path, &read_error);
        write_error(&message);
    } else if (result == LISP_RUN_ERROR) {
        Value *descriptor = lisp_take_error(state);
        lisp_buffer_append_string(&message, "error ");
        lisp_describe_error(&message, descriptor);
        lisp_release(descriptor);
        write_error(&message);
    }
    lisp_buffer_free(&message);
    return result == LISP_RUN_OK ? 0 : 1;
}

static void run(void *argument) {
    Run *request = argument;
    LispStateOptions options = {
        .output = lisp_output_to_stream(stdout),
        .input = lisp_input_from_stream(stdin),
        .stack_limit = lisp_large_stack_limit(),
    };
    LispState *state = lisp_state_new(&options);
    if (request->path) {
        request->exit_code = run_program(state, request->path);
    } else {
        bool interactive = isatty(STDIN_FILENO);
        if (interactive) setvbuf(stdout, nullptr, _IONBF, 0); /* show prompts before reading */
        LispReplOptions repl = {
            .errors = {.write = write_to_stderr, .context = nullptr},
            .prompt = interactive ? "> " : nullptr,
            .continuation_prompt = interactive ? "  " : nullptr,
        };
        lisp_repl(state, &repl);
        if (interactive) fputc('\n', stdout);
        request->exit_code = 0;
    }
    lisp_state_free(state);
    fflush(stdout);
}

int main(int argument_count, char **arguments) {
    Run request = {.path = nullptr, .exit_code = 0};
    if (argument_count == 2 && (strcmp(arguments[1], "-h") == 0 || strcmp(arguments[1], "--help") == 0)) {
        usage(stdout);
        return 0;
    }
    if (argument_count > 2 || (argument_count == 2 && arguments[1][0] == '-')) {
        usage(stderr);
        return 2;
    }
    if (argument_count == 2) request.path = arguments[1];
    /* Deep non-tail recursion uses the C stack: run on a large one. */
    if (!lisp_run_on_large_stack(run, &request)) {
        fputs("novelo: cannot create the interpreter thread\n", stderr);
        return 2;
    }
    return request.exit_code;
}
