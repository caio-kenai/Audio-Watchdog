#pragma once
// Minimal test harness (no external framework).
#ifndef AUDIOWATCHDOG_TEST_HARNESS_H
#define AUDIOWATCHDOG_TEST_HARNESS_H

#include <functional>
#include <string>
#include <vector>
#include <iostream>

namespace awtest {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

std::vector<TestCase>& RegisteredTests();

struct Registrar {
    Registrar(const char* name, std::function<void()> fn);
};

int RunAllTests();

// Throws natively which is caught by the runner and reported.
inline void Fail(const std::string& msg) { throw std::runtime_error(msg); }

template <typename A, typename B>
void CheckEqual(const A& a, const B& b, const char* file, int line) {
    if (!(a == b)) {
        throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + " check failed");
    }
}

inline void CheckImpl(bool cond, const char* file, int line, const char* expr) {
    if (!cond) {
        throw std::runtime_error(std::string(file) + ":" + std::to_string(line) +
                                 " condition false: " + expr);
    }
}

inline void CheckNearImpl(long long a, long long b, long long tol, const char* file, int line) {
    const long long d = a > b ? a - b : b - a;
    if (d > tol) {
        throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + " values not near");
    }
}

} // namespace awtest

#define TEST(name) \
    static void Test_##name(); \
    static awtest::Registrar reg_##name(#name, Test_##name); \
    static void Test_##name()

#define CHECK(expr) awtest::CheckImpl((expr), __FILE__, __LINE__, #expr)
#define CHECK_EQ(a, b) awtest::CheckEqual((a), (b), __FILE__, __LINE__)
#define CHECK_NEAR(a, b, tol) awtest::CheckNearImpl((a), (b), (tol), __FILE__, __LINE__)

#endif // AUDIOWATCHDOG_TEST_HARNESS_H