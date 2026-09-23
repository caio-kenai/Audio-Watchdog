#include "tests/test_harness.h"

#include <stdexcept>
#include <iostream>
#include <fstream>

#include <windows.h>

namespace awtest {

std::wstring TempFile(const wchar_t* name) {
    wchar_t dir[MAX_PATH] = {};
    ::GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + name;
}

std::vector<TestCase>& RegisteredTests() {
    static std::vector<TestCase> tests;
    return tests;
}

Registrar::Registrar(const char* name, std::function<void()> fn) {
    RegisteredTests().push_back({ name, std::move(fn) });
}

int RunAllTests() {
    int passed = 0;
    int failed = 0;
    std::ofstream trace(TempFile(L"aww_testtrace.txt"), std::ios::out | std::ios::trunc);
    for (const auto& t : RegisteredTests()) {
        trace << "RUN " << t.name << "\n";
        trace.flush();
        try {
            t.fn();
            ++passed;
            trace << "PASS " << t.name << "\n";
            std::cout << "[PASS] " << t.name << "\n";
        } catch (const std::exception& e) {
            ++failed;
            trace << "FAIL " << t.name << ": " << e.what() << "\n";
            std::cout << "[FAIL] " << t.name << ": " << e.what() << "\n";
        } catch (...) {
            ++failed;
            trace << "FAIL " << t.name << ": unknown exception\n";
            std::cout << "[FAIL] " << t.name << ": unknown exception\n";
        }
    }
    trace << "TOTAL " << RegisteredTests().size() << "\n";
    std::cout << "\n" << passed << " passed, " << failed << " failed, "
              << RegisteredTests().size() << " total\n";
    return failed == 0 ? 0 : 1;
}

} // namespace awtest