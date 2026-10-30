#include "shell_detection.h"

#include <array>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Only the cmd.exe and PowerShell image names select a shell")
{
  struct ImageFixture {
    std::string_view image;
    np::Shell expected;
  };
  constexpr auto k_fixtures = std::to_array<ImageFixture>({
      {.image = "cmd.exe", .expected = np::Shell::kCmd},
      {.image = "CMD.EXE", .expected = np::Shell::kCmd},
      {.image = "pwsh.exe", .expected = np::Shell::kPowerShell},
      {.image = "PowerShell.exe", .expected = np::Shell::kPowerShell},
      {.image = "powershell_ise.exe", .expected = np::Shell::kUnknown},
      {.image = "pwsh", .expected = np::Shell::kUnknown},
      {.image = "cmd.exe.exe", .expected = np::Shell::kUnknown},
      {.image = "bash.exe", .expected = np::Shell::kUnknown},
      {.image = "python.exe", .expected = np::Shell::kUnknown},
      {.image = "WindowsTerminal.exe", .expected = np::Shell::kUnknown},
      {.image = "", .expected = np::Shell::kUnknown},
  });
  for (const auto& fixture : k_fixtures)
  {
    DYNAMIC_SECTION(fixture.image)
    {
      CHECK(np::classifyShellImage(fixture.image) == fixture.expected);
    }
  }
}
