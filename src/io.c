/* Output and input implementations. */
#include "io.h"

#include <string.h>

static void write_to_buffer(void *context, const char *bytes, size_t length) {
    lisp_buffer_append((LispBuffer *)context, bytes, length);
}

LispOutput lisp_output_to_buffer(LispBuffer *buffer) {
    return (LispOutput){.write = write_to_buffer, .context = buffer};
}

static void write_to_stream(void *context, const char *bytes, size_t length) {
    FILE *stream = context;
    fwrite(bytes, 1, length, stream);
    fflush(stream);
}

LispOutput lisp_output_to_stream(FILE *stream) {
    return (LispOutput){.write = write_to_stream, .context = stream};
}

static bool read_line_from_text(void *context, LispBuffer *line) {
    LispTextInput *source = context;
    lisp_buffer_clear(line);
    if (source->position >= source->length) return false;
    while (source->position < source->length) {
        char byte = source->text[source->position++];
        if (byte == '\n') return true;
        lisp_buffer_append_byte(line, byte);
    }
    return true; /* a last line without U+000A */
}

LispInput lisp_input_from_text(LispTextInput *source) {
    return (LispInput){.read_line = read_line_from_text, .context = source};
}

static bool read_line_from_stream(void *context, LispBuffer *line) {
    FILE *stream = context;
    lisp_buffer_clear(line);
    int character = getc(stream);
    if (character == EOF) return false;
    while (character != EOF && character != '\n') {
        lisp_buffer_append_byte(line, (char)character);
        character = getc(stream);
    }
    return true;
}

LispInput lisp_input_from_stream(FILE *stream) {
    return (LispInput){.read_line = read_line_from_stream, .context = stream};
}

static bool read_nothing(void *, LispBuffer *line) {
    lisp_buffer_clear(line);
    return false;
}

LispInput lisp_input_empty(void) { return (LispInput){.read_line = read_nothing, .context = nullptr}; }

static bool read_from_disk(void *, const char *path, LispBuffer *contents) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    lisp_buffer_clear(contents);
    char chunk[65536];
    size_t length;
    while ((length = fread(chunk, 1, sizeof chunk, file)) > 0) lisp_buffer_append(contents, chunk, length);
    bool failed = ferror(file) != 0; /* e.g. the path is a directory */
    fclose(file);
    return !failed;
}

LispFileSystem lisp_file_system_disk(void) {
    return (LispFileSystem){.read_file = read_from_disk, .context = nullptr};
}

static bool read_from_memory(void *context, const char *path, LispBuffer *contents) {
    const LispMemoryFiles *files = context;
    for (size_t index = 0; index < files->count; index++) {
        if (strcmp(files->files[index].path, path) == 0) {
            lisp_buffer_clear(contents);
            lisp_buffer_append(contents, files->files[index].text, files->files[index].length);
            return true;
        }
    }
    return false;
}

LispFileSystem lisp_file_system_from_memory(const LispMemoryFiles *files) {
    return (LispFileSystem){.read_file = read_from_memory, .context = (void *)files};
}
