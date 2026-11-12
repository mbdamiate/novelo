/* Minimal unit test framework.
 *
 * Each *_test.c file exports one TestSuite, listed by hand in test.c.
 * Every test runs in a child process (unless --no-fork), so a crash counts as
 * a failure, and the live-object count must be the same before and after it.
 */
#pragma once

#include <stdint.h>
#include <string.h>

typedef void (*TestFn)(void);

typedef struct TestCase {
    const char *name;
    TestFn fn;
} TestCase;

typedef struct TestSuite {
    const char *name;
    const TestCase *cases; /* terminated by an entry with a null name */
} TestSuite;

[[gnu::format(printf, 3, 4)]]
void test_fail(const char *file, int line, const char *format, ...);

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) test_fail(__FILE__, __LINE__, "CHECK(%s)", #cond);      \
    } while (0)

#define CHECK_INT(a, b)                                                      \
    do {                                                                     \
        long long check_a_ = (long long)(a), check_b_ = (long long)(b);      \
        if (check_a_ != check_b_)                                            \
            test_fail(__FILE__, __LINE__, "CHECK_INT(%s, %s): %lld != %lld", \
                      #a, #b, check_a_, check_b_);                           \
    } while (0)

#define CHECK_STR(a, b)                                                      \
    do {                                                                     \
        const char *check_a_ = (a), *check_b_ = (b);                         \
        if (check_a_ == nullptr || check_b_ == nullptr                       \
            || strcmp(check_a_, check_b_) != 0)                              \
            test_fail(__FILE__, __LINE__, "CHECK_STR(%s, %s): \"%s\" != \"%s\"", \
                      #a, #b, check_a_ ? check_a_ : "(null)",                \
                      check_b_ ? check_b_ : "(null)");                       \
    } while (0)
