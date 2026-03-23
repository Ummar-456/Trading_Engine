#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <functional>

namespace test {

inline int g_pass{0};
inline int g_fail{0};
inline std::string g_current;

inline void run(const char* name, std::function<void()> fn) {
    g_current = name;
    try {
        fn();
        ++g_pass;
        std::cout << "  \033[32mPASS\033[0m " << name << '\n';
    } catch (const std::exception& e) {
        ++g_fail;
        std::cout << "  \033[31mFAIL\033[0m " << name << '\n'
                  << "       " << e.what() << '\n';
    }
}

inline int summary() {
    std::cout << '\n';
    if (g_fail == 0)
        std::cout << "\033[32m";
    else
        std::cout << "\033[31m";
    std::cout << "Passed: " << g_pass << " | Failed: " << g_fail
              << " | Total: " << (g_pass + g_fail) << "\033[0m\n";
    return g_fail > 0 ? 1 : 0;
}

} // namespace test

#define ASSERT_TRUE(expr) \
    do { if (!(expr)) { \
        std::ostringstream _o; \
        _o << "ASSERT_TRUE(" #expr ") at line " << __LINE__; \
        throw std::runtime_error(_o.str()); \
    } } while (0)

#define ASSERT_EQ(a, b) \
    do { auto _a = (a); auto _b = (b); \
    if (!(_a == _b)) { \
        std::ostringstream _o; \
        _o << "ASSERT_EQ " #a "==" #b " got " << _a << " != " << _b \
           << " at line " << __LINE__; \
        throw std::runtime_error(_o.str()); \
    } } while (0)

#define ASSERT_NE(a, b) \
    do { auto _a = (a); auto _b = (b); \
    if (_a == _b) { \
        std::ostringstream _o; \
        _o << "ASSERT_NE " #a "!=" #b " got equal " << _a \
           << " at line " << __LINE__; \
        throw std::runtime_error(_o.str()); \
    } } while (0)

#define ASSERT_LT(a, b) \
    do { auto _a = (a); auto _b = (b); \
    if (!(_a < _b)) { \
        std::ostringstream _o; \
        _o << "ASSERT_LT " #a "<" #b " got " << _a << " >= " << _b \
           << " at line " << __LINE__; \
        throw std::runtime_error(_o.str()); \
    } } while (0)

#define ASSERT_GE(a, b) \
    do { auto _a = (a); auto _b = (b); \
    if (!(_a >= _b)) { \
        std::ostringstream _o; \
        _o << "ASSERT_GE " #a ">=" #b " got " << _a << " < " << _b \
           << " at line " << __LINE__; \
        throw std::runtime_error(_o.str()); \
    } } while (0)

#define ASSERT_GT(a, b) \
    do { auto _a = (a); auto _b = (b); \
    if (!(_a > _b)) { \
        std::ostringstream _o; \
        _o << "ASSERT_GT " #a ">" #b " got " << _a << " <= " << _b \
           << " at line " << __LINE__; \
        throw std::runtime_error(_o.str()); \
    } } while (0)
