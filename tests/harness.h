/*
 * lightning - minimal test harness (no external dependencies).
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTNING_TEST_HARNESS_H
#define LIGHTNING_TEST_HARNESS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int lht_tests_run    = 0;
static int lht_tests_failed = 0;
static int lht_checks_run   = 0;
static int lht_checks_failed = 0;

#define LHT_CHECK(cond) do { \
    ++lht_checks_run; \
    if (!(cond)) { \
        ++lht_checks_failed; \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define LHT_CHECK_EQ(a, b) LHT_CHECK((a) == (b))
#define LHT_CHECK_NE(a, b) LHT_CHECK((a) != (b))
#define LHT_CHECK_STR(a, alen, b) LHT_CHECK((alen) == strlen(b) && memcmp((a), (b), (alen)) == 0)

#define LHT_TEST(name) static void name(void)
#define LHT_RUN(name) do { \
    int cf = lht_checks_failed; \
    printf("  [ RUN  ] %s\n", #name); \
    name(); \
    if (lht_checks_failed == cf) { \
        printf("  [  OK  ] %s\n", #name); \
    } else { \
        printf("  [ FAIL ] %s\n", #name); \
        ++lht_tests_failed; \
    } \
    ++lht_tests_run; \
} while (0)

#define LHT_TEST_MAIN() \
    int main(void) { \
        run_all(); \
        printf("\n%d checks, %d failed  |  %d tests, %d failed\n", \
               lht_checks_run, lht_checks_failed, lht_tests_run, lht_tests_failed); \
        return lht_checks_failed ? 1 : 0; \
    }

#endif
