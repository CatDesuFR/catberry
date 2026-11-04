// A tiny test harness: CHECK counts failures and says where they were.
#pragma once

#include <cstdio>

extern int g_checks, g_failures;

#define CHECK(expr)                                                                       \
    do {                                                                                  \
        ++g_checks;                                                                       \
        if (!(expr)) {                                                                    \
            ++g_failures;                                                                 \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
        }                                                                                 \
    } while (0)

void testMacro();
void testQuickClick();
void testPressMerger();
void testMergeInOrder();
