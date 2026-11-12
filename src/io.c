/* Output and input implementations. */
#include "io.h"

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
