/* Interfaces for standard output, standard input and the file system, so the
 * interpreter can run against the real ones or against memory (tests,
 * conformance). */
#pragma once

#include "buffer.h"

#include <stddef.h>
#include <stdio.h>

typedef struct LispOutput {
    void (*write)(void *context, const char *bytes, size_t length);
    void *context;
} LispOutput;

/* read_line stores the next line, without its U+000A, in *line and returns
 * true; at the end of input it returns false. Only U+000A ends a line, so a
 * U+000D stays in the line, and a last line without U+000A is returned as it
 * is (core §9, read-line!). */
typedef struct LispInput {
    bool (*read_line)(void *context, LispBuffer *line);
    void *context;
} LispInput;

/* Output appended to a buffer. */
LispOutput lisp_output_to_buffer(LispBuffer *buffer);

/* Output written to a stdio stream. */
LispOutput lisp_output_to_stream(FILE *stream);
/* Output that is thrown away. */
LispOutput lisp_output_discard(void);

/* Input read from text in memory; the text must outlive the input. */
typedef struct LispTextInput {
    const char *text;
    size_t length;
    size_t position;
} LispTextInput;

LispInput lisp_input_from_text(LispTextInput *source);

/* Input read from a stdio stream. */
LispInput lisp_input_from_stream(FILE *stream);

/* Input that is always at its end. */
LispInput lisp_input_empty(void);

/* core §8.8, §9 — the file system. Paths are absolute, normalized and use
 * '/'. Each operation says how it went; the library io turns the expected
 * failures into results and the others into :io-error. */
typedef enum LispFileStatus {
    LISP_FILE_OK,
    LISP_FILE_NOT_FOUND,         /* the path, or a directory on the way to it, does not exist */
    LISP_FILE_PERMISSION_DENIED,
    LISP_FILE_NOT_A_FILE,        /* a directory where a file was expected */
    LISP_FILE_NOT_A_DIRECTORY,   /* a file where a directory was expected */
    LISP_FILE_FAILED,            /* any other failure of the system */
} LispFileStatus;

typedef enum LispFileKind {
    LISP_FILE_KIND_NONE,
    LISP_FILE_KIND_FILE,
    LISP_FILE_KIND_DIRECTORY,
} LispFileKind;

typedef struct LispFileSystem {
    /* Stores the whole file in *contents. */
    LispFileStatus (*read_file)(void *context, const char *path, LispBuffer *contents);
    /* Creates or replaces the file, or with `append` adds to its end,
     * creating it if needed; its directory must exist. */
    LispFileStatus (*write_file)(void *context, const char *path, const char *bytes, size_t length, bool append);
    /* Stores the names of the directory's entries, each followed by a NUL,
     * in no particular order, without "." and "..". */
    LispFileStatus (*list_directory)(void *context, const char *path, LispBuffer *names);
    /* Stores what the path names; a path that does not exist is
     * LISP_FILE_KIND_NONE with LISP_FILE_OK. */
    LispFileStatus (*kind)(void *context, const char *path, LispFileKind *kind);
    void *context;
} LispFileSystem;

/* The disk. */
LispFileSystem lisp_file_system_disk(void);

/* A file given to an in-memory file system. */
typedef struct LispMemoryFile {
    const char *path; /* absolute */
    const char *text;
    size_t length;
} LispMemoryFile;

/* Files in memory, which the file system changes as the program writes.
 * The store owns copies of its paths and texts. A directory exists when it
 * is the root or when some file lies inside it; there is no permission. */
typedef struct LispMemoryStore {
    LispMemoryFile *files;
    size_t count;
    size_t capacity;
} LispMemoryStore;

/* Starts a store with copies of `count` files. */
void lisp_memory_store_init(LispMemoryStore *store, const LispMemoryFile *files, size_t count);
void lisp_memory_store_free(LispMemoryStore *store);

/* A file system over the store, which must outlive it. */
LispFileSystem lisp_file_system_from_memory(LispMemoryStore *store);
