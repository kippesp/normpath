#include <Windows.h>

#include <expected>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("The test executable runs with the UTF-8 process code page")
{
  REQUIRE(GetACP() == CP_UTF8);
}

TEST_CASE(
    "Owned sources and the standard library support the selected baseline")
{
  // Keep the final e + U+0301 decomposed (UTF-8 65 CC 81).
  constexpr std::expected<std::string_view, int> k_text = "漢字/проект/é";
  STATIC_REQUIRE(k_text.has_value());
  REQUIRE(k_text->size() == 23);
}
