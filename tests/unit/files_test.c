/* core §9 — the functions on files: the disk and memory file systems, and
 * the library io over the disk, including what the conformance cases cannot
 * reach (permissions, invalid UTF-8, failures of the system). */
#include "test.h"

#include "error.h"
#include "io.h"
#include "program.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

/* A temporary directory, removed with everything in it by remove_directory. */
static void make_directory(char *directory) { CHECK(mkdtemp(directory) != nullptr); }

static void remove_directory(const char *directory) {
    char command[600];
    snprintf(command, sizeof command, "chmod -R u+rwx '%s' && rm -rf '%s'", directory, directory);
    CHECK_INT(system(command), 0);
}

static void write_bytes(const char *directory, const char *name, const char *bytes, size_t length) {
    char path[600];
    snprintf(path, sizeof path, "%s/%s", directory, name);
    FILE *file = fopen(path, "wb");
    CHECK(file != nullptr);
    if (!file) return;
    fwrite(bytes, 1, length, file);
    fclose(file);
}

static char *path_in(const char *directory, const char *name) {
    static char path[600];
    snprintf(path, sizeof path, "%s/%s", directory, name);
    return path;
}

/* Runs a program with the working directory and file system given; returns
 * its value, or nullptr with *kind set to the :kind of its error. */
static Value *run_in(const char *directory, LispFileSystem files, const char *program, Value **kind) {
    LispBuffer output;
    lisp_buffer_init(&output);
    LispStateOptions options = {.output = lisp_output_to_buffer(&output), .files = files, .working_directory = directory};
    LispState *state = lisp_state_new(&options);
    Value *value = nullptr;
    LispReadError read_error;
    LispRunResult result = lisp_run_source(state, program, strlen(program), nullptr, &value, &read_error);
    *kind = result == LISP_RUN_ERROR ? lisp_error_kind(state) : nullptr; /* keywords are immortal */
    lisp_state_free(state);
    lisp_buffer_free(&output);
    return value;
}

static void t_disk_operations(void) {
    char directory[] = "/tmp/novelo-files-XXXXXX";
    make_directory(directory);
    LispFileSystem disk = lisp_file_system_disk();
    LispBuffer buffer;
    lisp_buffer_init(&buffer);

    CHECK_INT(disk.write_file(nullptr, path_in(directory, "a.txt"), "one", 3, false), LISP_FILE_OK);
    CHECK_INT(disk.write_file(nullptr, path_in(directory, "a.txt"), " two", 4, true), LISP_FILE_OK);
    CHECK_INT(disk.read_file(nullptr, path_in(directory, "a.txt"), &buffer), LISP_FILE_OK);
    CHECK_INT(buffer.length, 7);
    CHECK(memcmp(buffer.data, "one two", 7) == 0);
    CHECK_INT(disk.write_file(nullptr, path_in(directory, "a.txt"), "", 0, false), LISP_FILE_OK);
    CHECK_INT(disk.read_file(nullptr, path_in(directory, "a.txt"), &buffer), LISP_FILE_OK);
    CHECK_INT(buffer.length, 0);

    CHECK_INT(disk.read_file(nullptr, path_in(directory, "missing"), &buffer), LISP_FILE_NOT_FOUND);
    CHECK_INT(disk.read_file(nullptr, path_in(directory, "a.txt/inside"), &buffer), LISP_FILE_NOT_FOUND);
    CHECK_INT(disk.read_file(nullptr, directory, &buffer), LISP_FILE_NOT_A_FILE);
    CHECK_INT(disk.write_file(nullptr, directory, "x", 1, false), LISP_FILE_NOT_A_FILE);
    CHECK_INT(disk.write_file(nullptr, path_in(directory, "none/x"), "x", 1, false), LISP_FILE_NOT_FOUND);
    CHECK_INT(disk.list_directory(nullptr, path_in(directory, "a.txt"), &buffer), LISP_FILE_NOT_A_DIRECTORY);
    CHECK_INT(disk.list_directory(nullptr, path_in(directory, "missing"), &buffer), LISP_FILE_NOT_FOUND);
    CHECK_INT(disk.list_directory(nullptr, directory, &buffer), LISP_FILE_OK);
    CHECK_INT(buffer.length, strlen("a.txt") + 1); /* no "." or ".." */

    LispFileKind kind;
    CHECK_INT(disk.kind(nullptr, path_in(directory, "a.txt"), &kind), LISP_FILE_OK);
    CHECK_INT(kind, LISP_FILE_KIND_FILE);
    CHECK_INT(disk.kind(nullptr, directory, &kind), LISP_FILE_OK);
    CHECK_INT(kind, LISP_FILE_KIND_DIRECTORY);
    CHECK_INT(disk.kind(nullptr, path_in(directory, "missing"), &kind), LISP_FILE_OK);
    CHECK_INT(kind, LISP_FILE_KIND_NONE);

    /* a symbolic link that points at itself is a failure of the system */
    CHECK_INT(symlink("loop", path_in(directory, "loop")), 0);
    CHECK_INT(disk.read_file(nullptr, path_in(directory, "loop"), &buffer), LISP_FILE_FAILED);

    lisp_buffer_free(&buffer);
    remove_directory(directory);
}

/* Permissions do not hold for root, as in a container: then there is
 * nothing to check. */
static void t_disk_permissions(void) {
    if (geteuid() == 0) return;
    char directory[] = "/tmp/novelo-files-XXXXXX";
    make_directory(directory);
    LispFileSystem disk = lisp_file_system_disk();
    LispBuffer buffer;
    lisp_buffer_init(&buffer);
    write_bytes(directory, "secret.txt", "x", 1);
    chmod(path_in(directory, "secret.txt"), 0);
    CHECK_INT(disk.read_file(nullptr, path_in(directory, "secret.txt"), &buffer), LISP_FILE_PERMISSION_DENIED);
    CHECK_INT(disk.write_file(nullptr, path_in(directory, "secret.txt"), "y", 1, false), LISP_FILE_PERMISSION_DENIED);
    CHECK_INT(mkdir(path_in(directory, "closed"), 0), 0);
    CHECK_INT(disk.list_directory(nullptr, path_in(directory, "closed"), &buffer), LISP_FILE_PERMISSION_DENIED);
    LispFileKind kind;
    CHECK_INT(disk.kind(nullptr, path_in(directory, "closed/inside"), &kind), LISP_FILE_PERMISSION_DENIED);

    /* through the library: a result, and :io-error for file-kind! */
    Value *error_kind;
    Value *value = run_in(directory, disk,
                          "(import io)\n(list (read-file! \"secret.txt\") (list-directory! \"closed\"))", &error_kind);
    CHECK(value != nullptr);
    if (value) {
        CHECK_EQUAL(value, "((:error :permission-denied \"secret.txt\") (:error :permission-denied \"closed\"))");
    }
    lisp_release(value);
    value = run_in(directory, disk, "(import io)\n(file-kind! \"closed/inside\")", &error_kind);
    CHECK(value == nullptr);
    CHECK(error_kind == lisp_keyword_cstr("io-error"));

    lisp_buffer_free(&buffer);
    remove_directory(directory);
}

static void t_program_on_disk(void) {
    char directory[] = "/tmp/novelo-files-XXXXXX";
    make_directory(directory);
    LispFileSystem disk = lisp_file_system_disk();
    write_bytes(directory, "bad.txt", "ok\nab\xff", 6);
    write_bytes(directory, "b.txt", "", 0);
    write_bytes(directory, "C.txt", "", 0); /* not B.txt: macOS ignores case */
    write_bytes(directory, "\xc3\xa9.txt", "", 0);
    Value *kind;
    Value *value = run_in(directory, disk,
                          "(import io)\n(list (read-file! \"bad.txt\") (list-directory! \".\")"
                          " (write-file! \"new.txt\" \"made\") (read-file! \"new.txt\"))",
                          &kind);
    CHECK(value != nullptr);
    if (value) {
        CHECK_EQUAL(value, "((:error :invalid-utf8 \"bad.txt\" 2 3) (:ok (\"C.txt\" \"b.txt\" \"bad.txt\" \"é.txt\"))"
                           " (:ok #void) (:ok \"made\"))");
    }
    lisp_release(value);
    remove_directory(directory);
}

/* A name that is not UTF-8, which a disk such as APFS refuses to create, so
 * the memory store stands in for one that allows it. */
static void t_invalid_name(void) {
    LispMemoryFile files[] = {{"/\xff", "", 0}};
    LispMemoryStore store;
    lisp_memory_store_init(&store, files, 1);
    Value *kind;
    Value *value = run_in("/", lisp_file_system_from_memory(&store), "(import io)\n(list-directory! \".\")", &kind);
    CHECK(value == nullptr);
    CHECK(kind == lisp_keyword_cstr("io-error"));
    lisp_release(value);
    lisp_memory_store_free(&store);
}

/* A file system whose every operation fails: io raises :io-error. */
static LispFileStatus fail_read(void *, const char *, LispBuffer *) { return LISP_FILE_FAILED; }
static LispFileStatus fail_write(void *, const char *, const char *, size_t, bool) { return LISP_FILE_FAILED; }
static LispFileStatus fail_list(void *, const char *, LispBuffer *) { return LISP_FILE_FAILED; }
static LispFileStatus fail_kind(void *, const char *, LispFileKind *kind) {
    *kind = LISP_FILE_KIND_NONE;
    return LISP_FILE_FAILED;
}

static void t_system_failures(void) {
    LispFileSystem broken = {fail_read, fail_write, fail_list, fail_kind, nullptr};
    const char *programs[] = {
        "(import io)\n(read-file! \"a\")",       "(import io)\n(write-file! \"a\" \"x\")",
        "(import io)\n(append-file! \"a\" \"x\")", "(import io)\n(list-directory! \"a\")",
        "(import io)\n(file-kind! \"a\")",
    };
    for (size_t index = 0; index < sizeof programs / sizeof programs[0]; index++) {
        Value *kind;
        Value *value = run_in("/", broken, programs[index], &kind);
        CHECK(value == nullptr);
        CHECK(kind == lisp_keyword_cstr("io-error"));
        lisp_release(value);
    }
}

/* The memory store: a directory is listed once, whatever it holds. */
static void t_memory_store(void) {
    LispMemoryFile files[] = {
        {"/notes/a.txt", "a", 1},
        {"/notes/deep/b.txt", "b", 1},
        {"/notes/deep/c.txt", "c", 1},
    };
    LispMemoryStore store;
    lisp_memory_store_init(&store, files, 3);
    LispFileSystem memory = lisp_file_system_from_memory(&store);
    LispBuffer names;
    lisp_buffer_init(&names);
    CHECK_INT(memory.list_directory(memory.context, "/notes", &names), LISP_FILE_OK);
    CHECK_INT(names.length, strlen("a.txt") + 1 + strlen("deep") + 1);
    CHECK_INT(memory.write_file(memory.context, "/notes/a.txt/x", "x", 1, false), LISP_FILE_NOT_FOUND);
    CHECK_INT(memory.write_file(memory.context, "/top.txt", "x", 1, true), LISP_FILE_OK);
    CHECK_INT(store.count, 4);
    lisp_buffer_free(&names);
    lisp_memory_store_free(&store);
}

const TestSuite files_suite = {"files", (const TestCase[]){
    {"the disk", t_disk_operations},
    {"permissions on the disk", t_disk_permissions},
    {"a program on the disk", t_program_on_disk},
    {"a name that is not UTF-8", t_invalid_name},
    {"failures of the system", t_system_failures},
    {"the memory store", t_memory_store},
    {},
}};
