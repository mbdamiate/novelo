/* Output, input, file system and system implementations (core §8.8, §9). */
#include "io.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <time.h>

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

static void write_nowhere(void *, const char *, size_t) {}

LispOutput lisp_output_discard(void) {
    return (LispOutput){.write = write_nowhere, .context = nullptr};
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

/* ---- the disk ------------------------------------------------------------------------- */

static LispFileStatus status_from_errno(int number) {
    switch (number) {
    case ENOENT:
    case ENOTDIR:
    case ENAMETOOLONG:
        return LISP_FILE_NOT_FOUND;
    case EACCES:
    case EPERM:
    case EROFS:
        return LISP_FILE_PERMISSION_DENIED;
    case EISDIR:
        return LISP_FILE_NOT_A_FILE;
    default:
        return LISP_FILE_FAILED;
    }
}

static LispFileStatus read_from_disk(void *, const char *path, LispBuffer *contents) {
    FILE *file = fopen(path, "rb");
    if (!file) return status_from_errno(errno);
    struct stat information;
    if (fstat(fileno(file), &information) == 0 && S_ISDIR(information.st_mode)) {
        fclose(file);
        return LISP_FILE_NOT_A_FILE;
    }
    lisp_buffer_clear(contents);
    char chunk[65536];
    size_t length;
    while ((length = fread(chunk, 1, sizeof chunk, file)) > 0) lisp_buffer_append(contents, chunk, length);
    LispFileStatus status = ferror(file) ? status_from_errno(errno) : LISP_FILE_OK;
    fclose(file);
    return status;
}

static LispFileStatus write_to_disk(void *, const char *path, const char *bytes, size_t length, bool append) {
    FILE *file = fopen(path, append ? "ab" : "wb");
    if (!file) return status_from_errno(errno);
    bool written = fwrite(bytes, 1, length, file) == length;
    bool closed = fclose(file) == 0; /* flushes: a full disk shows here */
    return written && closed ? LISP_FILE_OK : status_from_errno(errno);
}

static LispFileStatus list_disk_directory(void *, const char *path, LispBuffer *names) {
    DIR *directory = opendir(path);
    if (!directory) return errno == ENOTDIR ? LISP_FILE_NOT_A_DIRECTORY : status_from_errno(errno);
    lisp_buffer_clear(names);
    errno = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        lisp_buffer_append(names, entry->d_name, strlen(entry->d_name) + 1);
    }
    LispFileStatus status = errno != 0 ? status_from_errno(errno) : LISP_FILE_OK;
    closedir(directory);
    return status;
}

static LispFileStatus disk_kind(void *, const char *path, LispFileKind *kind) {
    struct stat information;
    *kind = LISP_FILE_KIND_NONE;
    if (stat(path, &information) != 0) {
        LispFileStatus status = status_from_errno(errno);
        return status == LISP_FILE_NOT_FOUND ? LISP_FILE_OK : status;
    }
    *kind = S_ISDIR(information.st_mode) ? LISP_FILE_KIND_DIRECTORY : LISP_FILE_KIND_FILE;
    return LISP_FILE_OK;
}

LispFileSystem lisp_file_system_disk(void) {
    return (LispFileSystem){
        .read_file = read_from_disk,
        .write_file = write_to_disk,
        .list_directory = list_disk_directory,
        .kind = disk_kind,
        .context = nullptr,
    };
}

/* ---- memory ------------------------------------------------------------------------------ */

static char *copy_text(const char *text, size_t length) {
    char *copy = lisp_xmalloc(length + 1);
    if (length > 0) memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static void store_add(LispMemoryStore *store, const char *path, const char *text, size_t length) {
    if (store->count == store->capacity) {
        store->capacity = store->capacity ? store->capacity * 2 : 8;
        store->files = lisp_xrealloc(store->files, store->capacity * sizeof(LispMemoryFile));
    }
    store->files[store->count++] = (LispMemoryFile){
        .path = copy_text(path, strlen(path)),
        .text = copy_text(text, length),
        .length = length,
    };
}

void lisp_memory_store_init(LispMemoryStore *store, const LispMemoryFile *files, size_t count) {
    *store = (LispMemoryStore){};
    for (size_t index = 0; index < count; index++) {
        store_add(store, files[index].path, files[index].text, files[index].length);
    }
}

void lisp_memory_store_free(LispMemoryStore *store) {
    for (size_t index = 0; index < store->count; index++) {
        free((char *)store->files[index].path);
        free((char *)store->files[index].text);
    }
    free(store->files);
    *store = (LispMemoryStore){};
}

static LispMemoryFile *find_file(LispMemoryStore *store, const char *path) {
    for (size_t index = 0; index < store->count; index++) {
        if (strcmp(store->files[index].path, path) == 0) return &store->files[index];
    }
    return nullptr;
}

/* The part of `path` inside `directory`, or nullptr when it is not inside. */
static const char *inside(const char *path, const char *directory) {
    if (strcmp(directory, "/") == 0) return path[0] == '/' ? path + 1 : nullptr;
    size_t length = strlen(directory);
    if (strncmp(path, directory, length) != 0 || path[length] != '/') return nullptr;
    return path + length + 1;
}

static bool is_directory(LispMemoryStore *store, const char *path) {
    if (strcmp(path, "/") == 0) return true;
    for (size_t index = 0; index < store->count; index++) {
        if (inside(store->files[index].path, path)) return true;
    }
    return false;
}

static LispFileStatus read_from_memory(void *context, const char *path, LispBuffer *contents) {
    LispMemoryStore *store = context;
    LispMemoryFile *file = find_file(store, path);
    if (!file) return is_directory(store, path) ? LISP_FILE_NOT_A_FILE : LISP_FILE_NOT_FOUND;
    lisp_buffer_clear(contents);
    lisp_buffer_append(contents, file->text, file->length);
    return LISP_FILE_OK;
}

static LispFileStatus write_to_memory(void *context, const char *path, const char *bytes, size_t length,
                                      bool append) {
    LispMemoryStore *store = context;
    if (is_directory(store, path)) return LISP_FILE_NOT_A_FILE;
    LispMemoryFile *file = find_file(store, path);
    if (!file) {
        /* the directory must exist: the root, or one that holds a file */
        const char *slash = strrchr(path, '/');
        char *parent = copy_text(path, slash == path ? 1 : (size_t)(slash - path));
        bool exists = is_directory(store, parent);
        free(parent);
        if (!exists) return LISP_FILE_NOT_FOUND;
        store_add(store, path, bytes, length);
        return LISP_FILE_OK;
    }
    size_t kept = append ? file->length : 0;
    char *text = lisp_xmalloc(kept + length + 1);
    if (kept > 0) memcpy(text, file->text, kept);
    if (length > 0) memcpy(text + kept, bytes, length);
    text[kept + length] = '\0';
    free((char *)file->text);
    file->text = text;
    file->length = kept + length;
    return LISP_FILE_OK;
}

static LispFileStatus list_memory_directory(void *context, const char *path, LispBuffer *names) {
    LispMemoryStore *store = context;
    if (find_file(store, path)) return LISP_FILE_NOT_A_DIRECTORY;
    if (!is_directory(store, path)) return LISP_FILE_NOT_FOUND;
    lisp_buffer_clear(names);
    for (size_t index = 0; index < store->count; index++) {
        const char *rest = inside(store->files[index].path, path);
        if (!rest) continue;
        const char *slash = strchr(rest, '/');
        size_t length = slash ? (size_t)(slash - rest) : strlen(rest);
        /* a directory with several files is listed once */
        bool seen = false;
        for (size_t offset = 0; offset < names->length && !seen; offset += strlen(names->data + offset) + 1) {
            seen = strlen(names->data + offset) == length && memcmp(names->data + offset, rest, length) == 0;
        }
        if (seen) continue;
        lisp_buffer_append(names, rest, length);
        lisp_buffer_append_byte(names, '\0');
    }
    return LISP_FILE_OK;
}

static LispFileStatus memory_kind(void *context, const char *path, LispFileKind *kind) {
    LispMemoryStore *store = context;
    *kind = find_file(store, path)      ? LISP_FILE_KIND_FILE
            : is_directory(store, path) ? LISP_FILE_KIND_DIRECTORY
                                        : LISP_FILE_KIND_NONE;
    return LISP_FILE_OK;
}

LispFileSystem lisp_file_system_from_memory(LispMemoryStore *store) {
    return (LispFileSystem){
        .read_file = read_from_memory,
        .write_file = write_to_memory,
        .list_directory = list_memory_directory,
        .kind = memory_kind,
        .context = store,
    };
}

/* ---- the system (core §9) ------------------------------------------------------------------ */

static const char *real_environment_variable(void *, const char *name) { return getenv(name); }

static bool real_current_time(void *, int64_t *milliseconds) {
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) return false;
    *milliseconds = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    return true;
}

static bool real_monotonic_time(void *, int64_t *nanoseconds) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return false;
    *nanoseconds = (int64_t)now.tv_sec * 1000000000 + now.tv_nsec;
    return true;
}

static bool real_random_seed(void *, int64_t *seed) {
    uint64_t bits;
    if (getentropy(&bits, sizeof bits) != 0) return false;
    *seed = (int64_t)(bits >> 1);
    return true;
}

LispSystem lisp_system_real(void) {
    return (LispSystem){
        .environment_variable = real_environment_variable,
        .current_time = real_current_time,
        .monotonic_time = real_monotonic_time,
        .random_seed = real_random_seed,
        .context = nullptr,
    };
}

static const char *fixed_environment_variable(void *context, const char *name) {
    LispFixedSystem *system = context;
    for (size_t index = 0; index < system->variable_count; index++) {
        if (strcmp(system->variables[index].name, name) == 0) return system->variables[index].value;
    }
    return nullptr;
}

static bool fixed_current_time(void *context, int64_t *milliseconds) {
    *milliseconds = ((LispFixedSystem *)context)->current_time;
    return true;
}

/* The next of `count` values, the last one again once they run out. */
static int64_t next_fixed(const int64_t *values, size_t count, size_t *next) {
    if (count == 0) return 0;
    int64_t value = values[*next];
    if (*next + 1 < count) (*next)++;
    return value;
}

static bool fixed_monotonic_time(void *context, int64_t *nanoseconds) {
    LispFixedSystem *system = context;
    *nanoseconds = next_fixed(system->monotonic_times, system->monotonic_count, &system->monotonic_next);
    return true;
}

static bool fixed_random_seed(void *context, int64_t *seed) {
    LispFixedSystem *system = context;
    *seed = next_fixed(system->seeds, system->seed_count, &system->seed_next);
    return true;
}

LispSystem lisp_system_fixed(LispFixedSystem *system) {
    return (LispSystem){
        .environment_variable = fixed_environment_variable,
        .current_time = fixed_current_time,
        .monotonic_time = fixed_monotonic_time,
        .random_seed = fixed_random_seed,
        .context = system,
    };
}
