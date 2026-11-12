/* Minimal unit test framework.
 *
 * Each *_test.c file exports one TestSuite, listed by hand in test.c.
 * Every test runs in a child process (unless --no-fork), so a crash counts as
 * a failure, and the live-object count must be the same before and after it.
 */
#pragma once

#include <stdint.h>
#include <string.h>

typedef void (*TestFunction)(void);

typedef struct TestCase {
    const char *name;
    TestFunction function;
} TestCase;

typedef struct TestSuite {
    const char *name;
    const TestCase *cases; /* terminated by an entry with a null name */
} TestSuite;

[[gnu::format(printf, 3, 4)]]
void test_fail(const char *file, int line, const char *format, ...);

typedef struct Value Value;
void test_check_equal(const char *file, int line, const Value *value, const char *expected_text);
void test_check_read_error(const char *file, int line, const char *source, const char *code,
                           int expected_line, int expected_column);

#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) test_fail(__FILE__, __LINE__, "CHECK(%s)", #condition);     \
    } while (0)

#define CHECK_INT(left, right)                                                        \
    do {                                                                              \
        long long check_left_ = (long long)(left);                                    \
        long long check_right_ = (long long)(right);                                  \
        if (check_left_ != check_right_)                                              \
            test_fail(__FILE__, __LINE__, "CHECK_INT(%s, %s): %lld != %lld",          \
                      #left, #right, check_left_, check_right_);                      \
    } while (0)

#define CHECK_STR(left, right)                                                        \
    do {                                                                              \
        const char *check_left_ = (left);                                             \
        const char *check_right_ = (right);                                           \
        if (check_left_ == nullptr || check_right_ == nullptr                         \
            || strcmp(check_left_, check_right_) != 0)                                \
            test_fail(__FILE__, __LINE__, "CHECK_STR(%s, %s): \"%s\" != \"%s\"",      \
                      #left, #right, check_left_ ? check_left_ : "(null)",            \
                      check_right_ ? check_right_ : "(null)");                        \
    } while (0)

/* The value is equal? to the single datum read from expected_text. */
#define CHECK_EQUAL(value, expected_text) \
    test_check_equal(__FILE__, __LINE__, (value), (expected_text))

/* Reading source fails with this code (e.g. "unclosed-delimiter") at line:column. */
#define CHECK_READ_ERROR(source, code, line, column) \
    test_check_read_error(__FILE__, __LINE__, (source), (code), (line), (column))
