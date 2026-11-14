#pragma once

#include <cstdio>

namespace test {

inline int failures = 0;

inline void expect(bool condition, const char* expression, const char* file, int line)
{
    if (!condition) {
        std::fprintf(stderr, "%s:%d: expectation failed: %s\n", file, line, expression);
        ++failures;
    }
}

}  // namespace test

#define EXPECT_TRUE(expression) \
    ::test::expect(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
#define EXPECT_FALSE(expression) EXPECT_TRUE(!(expression))
