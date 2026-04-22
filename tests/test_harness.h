#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern int tests_run;
extern int tests_passed;
extern int tests_failed;
extern int _test_failed_flag;

#define TEST(name) static void test_##name(void)

#define RUN_TEST(name) do { \
    tests_run++; \
    _test_failed_flag = 0; \
    printf("  %-50s ", #name); \
    test_##name(); \
    if (_test_failed_flag) { \
        tests_failed++; \
    } else { \
        tests_passed++; \
        printf("PASS\n"); \
    } \
} while (0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        printf("FAIL\n    %s:%d: ASSERT_TRUE(%s)\n", __FILE__, __LINE__, #expr); \
        _test_failed_flag = 1; \
        return; \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    long long _a = (long long)(a); \
    long long _b = (long long)(b); \
    if (_a != _b) { \
        printf("FAIL\n    %s:%d: ASSERT_EQ(%s, %s) — got %lld, expected %lld\n", \
               __FILE__, __LINE__, #a, #b, _a, _b); \
        _test_failed_flag = 1; \
        return; \
    } \
} while (0)

#define ASSERT_EQ_HEX(a, b) do { \
    unsigned long long _a = (unsigned long long)(a); \
    unsigned long long _b = (unsigned long long)(b); \
    if (_a != _b) { \
        printf("FAIL\n    %s:%d: ASSERT_EQ_HEX(%s, %s) — got 0x%llX, expected 0x%llX\n", \
               __FILE__, __LINE__, #a, #b, _a, _b); \
        _test_failed_flag = 1; \
        return; \
    } \
} while (0)

#define ASSERT_STR_EQ(a, b) do { \
    const char* _a = (a); \
    const char* _b = (b); \
    if (strcmp(_a, _b) != 0) { \
        printf("FAIL\n    %s:%d: ASSERT_STR_EQ(%s, %s) — got \"%s\", expected \"%s\"\n", \
               __FILE__, __LINE__, #a, #b, _a, _b); \
        _test_failed_flag = 1; \
        return; \
    } \
} while (0)

#define ASSERT_MEM_EQ(a, b, len) do { \
    if (memcmp((a), (b), (len)) != 0) { \
        printf("FAIL\n    %s:%d: ASSERT_MEM_EQ(%s, %s, %zu)\n", \
               __FILE__, __LINE__, #a, #b, (size_t)(len)); \
        _test_failed_flag = 1; \
        return; \
    } \
} while (0)

#define ASSERT_NEQ(a, b) do { \
    long long _a = (long long)(a); \
    long long _b = (long long)(b); \
    if (_a == _b) { \
        printf("FAIL\n    %s:%d: ASSERT_NEQ(%s, %s) — both are %lld\n", \
               __FILE__, __LINE__, #a, #b, _a); \
        _test_failed_flag = 1; \
        return; \
    } \
} while (0)

#define TEST_SUITE(name) printf("[%s]\n", name)

#endif // TEST_HARNESS_H
