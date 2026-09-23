#include "tests/test_harness.h"

#include "util/StrUtil.h"

using namespace aw;

TEST(StrUtil_WideUtf8Roundtrip) {
    const std::wstring wide = L"Audição — watcher d\'água ✓";
    const std::string utf8 = Utf8FromWide(wide);
    CHECK_EQ(WideFromUtf8(utf8), wide);
}

TEST(StrUtil_Trim) {
    CHECK_EQ(TrimView(L"  hello \t\n"), std::wstring(L"hello"));
    CHECK_EQ(TrimView(L"\r\nx\r\n"), std::wstring(L"x"));
    CHECK_EQ(TrimView(L""), std::wstring(L""));
}

TEST(StrUtil_LowerA) {
    CHECK_EQ(ToLowerA(L"AbCDeF"), std::wstring(L"abcdef"));
    CHECK_EQ(ToLowerA(L"Already123"), std::wstring(L"already123"));
}

TEST(StrUtil_Split) {
    auto parts = Split(L"a,b,c", L',');
    CHECK_EQ(parts.size(), size_t(3));
    CHECK_EQ(parts[0], std::wstring(L"a"));
    CHECK_EQ(parts[2], std::wstring(L"c"));
}

TEST(StrUtil_FormatAndHr) {
    CHECK_EQ(FormatW(L"val=%d", 7), std::wstring(L"val=7"));
    CHECK_EQ(HrToHex(0x80004005), std::string("0x80004005"));
    // HrText should produce something non-empty.
    CHECK(!HrText(S_OK).empty());
}