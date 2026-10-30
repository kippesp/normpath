#include "path_conversion.h"

#include <Windows.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

namespace {

struct Fixture {
  std::string_view input;
  std::optional<std::string_view> expected;
};

void checkPathConversion(const Fixture& a_fixture,
                         std::optional<std::string_view> a_home = std::nullopt,
                         np::PathStyle a_style = np::PathStyle::kCmd)
{
  const auto k_result = np::convertPath(a_fixture.input, a_style, a_home);
  if (a_fixture.expected)
  {
    REQUIRE(k_result.has_value());
    CHECK(*k_result == *a_fixture.expected);
    return;
  }
  REQUIRE(!k_result.has_value());
  CHECK(!k_result.error().reason.empty());
}

}  // namespace

TEST_CASE("The conversion executable has the UTF-8 process code page")
{
  REQUIRE(GetACP() == CP_UTF8);
}

TEST_CASE(
    "Narrow Windows normalization preserves Unicode bytes and root identity")
{
  constexpr auto k_fixtures = std::to_array<Fixture>({
      {.input = "C:/Users/漢字/проект", .expected = "C:\\Users\\漢字\\проект"},
      {
          .input = "C:/café",
          .expected = "C:\\café",
      },  // e + U+0301 stays decomposed.
      {.input = "./c:/foo", .expected = std::nullopt},
      {.input = "a/../c:/foo", .expected = std::nullopt},
      {.input = "./C:foo", .expected = std::nullopt},
      {
          .input = R"(\\server\share\dir\..\file)",
          .expected = R"(\\server\share\file)",
      },
      {.input = R"(\\server\share\dir\..)", .expected = R"(\\server\share\)"},
      {.input = R"(\\server\share\..\other)", .expected = std::nullopt},
      {.input = R"(\\server\share\..)", .expected = std::nullopt},
      {.input = "///a/b", .expected = R"(\a\b)"},
      {.input = "///c/foo", .expected = R"(\c\foo)"},
      {.input = "///mnt/c/foo", .expected = R"(\mnt\c\foo)"},
  });
  for (const auto& fixture : k_fixtures)
  {
    DYNAMIC_SECTION(fixture.input)
    {
      checkPathConversion(fixture);
    }
  }
}

TEST_CASE("Home tokens use only the supplied value and append the literal tail")
{
  struct HomeFixture {
    Fixture path;
    std::optional<std::string_view> home;
  };
  constexpr auto k_fixtures = std::to_array<HomeFixture>({
      {
          .path =
              {
                  .input = "~/mystuff/projects/cli_utils/normpath.git",
                  .expected =
                      R"(C:\Users\me\mystuff\projects\cli_utils\normpath.git)",
              },
          .home = R"(C:\Users\me)",
      },
      {
          .path = {.input = "~", .expected = R"(C:\Users\me)"},
          .home = R"(C:\Users\me)",
      },
      {
          .path = {.input = "~/", .expected = R"(C:\Users\me)"},
          .home = R"(C:\Users\me)",
      },
      {
          .path = {.input = R"(~\src)", .expected = R"(C:\Users\me\src)"},
          .home = R"(C:\Users\me)",
      },
      {
          .path =
              {
                  .input = "~/Documents",
                  .expected = R"(C:\Users\me\Documents)",
              },
          .home = R"(C:\Users\me)",
      },
      {
          .path = {.input = "~/x", .expected = R"(C:\Users\me\x)"},
          .home = R"(C:\Users\me\)",
      },
      {
          .path = {.input = "~/x", .expected = R"("C:\Users\Jo Doe\x")"},
          .home = R"(C:\Users\Jo Doe)",
      },
      {
          .path = {.input = "~/x", .expected = "C:\\Users\\Зоя\\x"},
          .home = "C:\\Users\\Зоя",
      },
      {
          .path = {.input = "~user/x", .expected = R"(~user\x)"},
          .home = std::nullopt,
      },
      {.path = {.input = "~/foo", .expected = std::nullopt}, .home = "C:Users"},
      {
          .path = {.input = "~/x", .expected = R"(D:\home\x)"},
          .home = "/d:/home",
      },
      {
          .path = {.input = "~", .expected = R"(D:\)"},
          .home = "/d:",
      },
      {
          .path = {.input = "~/../x", .expected = std::nullopt},
          .home = "/d:home",
      },
      {
          .path = {.input = "~/x", .expected = std::nullopt},
          .home = std::nullopt,
      },
      {.path = {.input = "~/x", .expected = std::nullopt}, .home = ""},
      {
          .path = {.input = "$HOME/x", .expected = R"($HOME\x)"},
          .home = std::nullopt,
      },
      {
          .path =
              {
                  .input = "%USERPROFILE%/x",
                  .expected = R"(%USERPROFILE%\x)",
              },
          .home = std::nullopt,
      },
      {
          .path = {.input = "${HOME}/x", .expected = R"(${HOME}\x)"},
          .home = "C:/ignored",
      },
      {.path = {.input = "~", .expected = std::nullopt}, .home = R"(\\server)"},
      {
          .path = {.input = "~/x", .expected = R"(\\server\x)"},
          .home = R"(\\server)",
      },
      {
          .path = {.input = "~/x", .expected = std::nullopt},
          .home = R"(\\?\C:\home)",
      },
      {
          .path = {.input = "~/x", .expected = std::nullopt},
          .home = "C:/invalid?",
      },
      {
          .path = {.input = "~/x", .expected = R"(relative\home\x)"},
          .home = "relative/home",
      },
  });
  for (const auto& fixture : k_fixtures)
  {
    DYNAMIC_SECTION(fixture.path.input << " home="
                                       << fixture.home.value_or("<absent>"))
    {
      checkPathConversion(fixture.path, fixture.home);
    }
  }

  for (const auto* const k_token : {"~", "~/", "~/x", R"(~\x)"})
  {
    CHECK(np::startsWithHomeToken(k_token));
  }
  for (const auto* const k_text : {"", "~user", "~user/x", "x/~", "~~/x"})
  {
    CHECK(!np::startsWithHomeToken(k_text));
  }
}

TEST_CASE("Root interpretation rejects context dependent and malformed roots")
{
  constexpr auto k_fixtures = std::to_array<Fixture>({
      {.input = "a/b/c", .expected = R"(a\b\c)"},
      {.input = "/users/me", .expected = R"(\users\me)"},
      {.input = R"(\logs/x)", .expected = R"(\logs\x)"},
      {.input = "/", .expected = R"(\)"},
      {.input = "C:/a/b", .expected = R"(C:\a\b)"},
      {.input = "d:/a/b", .expected = R"(D:\a\b)"},
      {.input = "C:/", .expected = R"(C:\)"},
      {.input = "/c/foo", .expected = R"(C:\foo)"},
      {.input = "/C/foo", .expected = R"(C:\foo)"},
      {.input = "/mnt/d/foo", .expected = R"(D:\foo)"},
      {.input = "/c", .expected = R"(C:\)"},
      {.input = "/mnt/c/", .expected = R"(C:\)"},
      {.input = "/mnt", .expected = R"(\mnt)"},
      {.input = "/mnt/cc/x", .expected = R"(\mnt\cc\x)"},
      {.input = "/C:/home/me", .expected = R"(C:\home\me)"},
      {.input = "/c:", .expected = R"(C:\)"},
      {.input = R"(/d:\x)", .expected = R"(D:\x)"},
      {.input = "/C:foo", .expected = std::nullopt},
      {.input = "/C:/../x", .expected = R"(C:\x)"},
      {.input = "/C:/./../x", .expected = R"(C:\x)"},
      {.input = "/C:/a/../../x", .expected = R"(C:\x)"},
      {.input = "/C:foo/../x", .expected = std::nullopt},
      {.input = "/C:foo/a/../../x", .expected = std::nullopt},
      {.input = "//C:/x", .expected = R"(\\C:\x)"},
      {.input = R"(\C:\x)", .expected = std::nullopt},
      {.input = "/C:/", .expected = R"(C:\)"},
      {.input = "/z://a/../", .expected = R"(Z:\)"},
      {.input = "/C::/../x", .expected = std::nullopt},
      {.input = "/mnt/c:/x", .expected = std::nullopt},
      {.input = "/mnt/c:/../x", .expected = R"(\mnt\x)"},
      {.input = "/1:/x", .expected = std::nullopt},
      {.input = "/1:/../x", .expected = R"(\x)"},
      {.input = "/é:/x", .expected = std::nullopt},
      {.input = "/é:/../x", .expected = R"(\x)"},
      {.input = "///C:/x", .expected = std::nullopt},
      {.input = "///C:/../x", .expected = R"(\x)"},
      {.input = R"(\C:\..\x)", .expected = R"(\x)"},
      {.input = R"(\\server\share\dir)", .expected = R"(\\server\share\dir)"},
      {.input = "//server/share", .expected = R"(\\server\share)"},
      {.input = "//c/foo", .expected = R"(\\c\foo)"},
      {
          .input = R"(\\server\my share\x)",
          .expected = R"("\\server\my share\x")",
      },
      {.input = R"(\\server\con\x)", .expected = R"(\\server\con\x)"},
      {.input = "\\\\srv\nx\\share", .expected = std::nullopt},
      {.input = "\\\\server\\sh\tare", .expected = std::nullopt},
      {.input = "C:foo", .expected = std::nullopt},
      {.input = "C:", .expected = std::nullopt},
      {.input = R"(\\?\C:\x)", .expected = std::nullopt},
      {.input = R"(\\.\COM1)", .expected = std::nullopt},
      {.input = "//?/C:/x", .expected = std::nullopt},
      {.input = R"(\\server)", .expected = std::nullopt},
      {.input = R"(\\server\)", .expected = std::nullopt},
      {.input = R"(\\)", .expected = std::nullopt},
      {.input = R"(\\\\)", .expected = std::nullopt},
      {.input = R"(\x)", .expected = R"(\x)"},
      {.input = "", .expected = std::nullopt},
      {.input = R"(\/server/share/x)", .expected = R"(\\server\share\x)"},
      {.input = R"(\\server\share.\x)", .expected = R"(\\server\share.\x)"},
      {.input = R"(\\server\sh:are\x)", .expected = R"(\\server\sh:are\x)"},
      {.input = R"(\\con\share\x)", .expected = R"(\\con\share\x)"},
      {.input = R"(\\server\share\con)", .expected = std::nullopt},
  });
  for (const auto& fixture : k_fixtures)
  {
    DYNAMIC_SECTION(fixture.input)
    {
      checkPathConversion(fixture);
    }
  }
}

TEST_CASE("SMB URLs use ordinary UNC normalization without URL decoding")
{
  constexpr auto k_fixtures = std::to_array<Fixture>({
      {
          .input = "smb://server/share/漢字/проект",
          .expected = "\\\\server\\share\\漢字\\проект",
      },
      {
          .input = "smb://192.168.1.86/share",
          .expected = R"(\\192.168.1.86\share)",
      },
      {.input = "smb://mycomputer/share", .expected = R"(\\mycomputer\share)"},
      {
          .input = "smb://mycomputer.company.com/share/docs",
          .expected = R"(\\mycomputer.company.com\share\docs)",
      },
      {
          .input = "SmB://Server/Share/dir/../file",
          .expected = R"(\\Server\Share\file)",
      },
      {
          .input = "smb://server/my share/my docs",
          .expected = R"("\\server\my share\my docs")",
      },
      {.input = "smb://server/share/", .expected = R"(\\server\share\)"},
      {
          .input = "smb://server/share/dir//./file/",
          .expected = R"(\\server\share\dir\file)",
      },
      {.input = "smb://server/share/../other", .expected = std::nullopt},
      {.input = "smb://server/share/..", .expected = std::nullopt},
      {.input = "smb://server/share/con.txt", .expected = std::nullopt},
      {
          .input = "smb://server/share/name.",
          .expected = R"(\\server\share\name)",
      },
      {.input = "smb:", .expected = std::nullopt},
      {.input = "smb:/server/share", .expected = std::nullopt},
      {.input = R"(smb:\\server\share)", .expected = std::nullopt},
      {.input = R"(smb://server/share\file)", .expected = std::nullopt},
      {.input = "smb:///share", .expected = std::nullopt},
      {.input = "smb://server", .expected = std::nullopt},
      {.input = "smb://server/", .expected = std::nullopt},
      {.input = "smb://server//share", .expected = std::nullopt},
      {.input = "smb://user@server/share", .expected = std::nullopt},
      {.input = "smb://server:445/share", .expected = std::nullopt},
      {.input = "smb://[::1]/share", .expected = std::nullopt},
      {.input = "smb://server/share?query", .expected = std::nullopt},
      {.input = "smb://server/share/file#fragment", .expected = std::nullopt},
      {.input = "smb://server/share/a%20b", .expected = std::nullopt},
      {.input = "smb://server/sh%61re", .expected = std::nullopt},
      {.input = "smb://ser%76er/share", .expected = std::nullopt},
      {.input = "smb://server/share/100%", .expected = std::nullopt},
      {.input = "smb://server/share/bad%/../ok", .expected = std::nullopt},
      {
          .input = R"(\\server:445\share%\file#name)",
          .expected = R"(\\server:445\share%\file#name)",
      },
  });
  for (const auto& fixture : k_fixtures)
  {
    DYNAMIC_SECTION(fixture.input)
    {
      checkPathConversion(fixture);
    }
  }
}

TEST_CASE("Whole path normalization preserves root trailing separators")
{
  constexpr auto k_fixtures = std::to_array<Fixture>({
      {.input = "a//b", .expected = R"(a\b)"},
      {.input = R"(C:\\a\\\b)", .expected = R"(C:\a\b)"},
      {.input = "a/./b", .expected = R"(a\b)"},
      {.input = "a/../b", .expected = "b"},
      {.input = "../b", .expected = R"(..\b)"},
      {.input = "a/..", .expected = "."},
      {.input = ".", .expected = "."},
      {.input = "./", .expected = "."},
      {.input = "C:/Users/me/", .expected = R"(C:\Users\me)"},
      {.input = R"(C:\)", .expected = R"(C:\)"},
      {.input = R"(\\server\share\)", .expected = R"(\\server\share\)"},
      {.input = R"(\\server\share)", .expected = R"(\\server\share)"},
      {.input = "c:/Foo/BAR", .expected = R"(C:\Foo\BAR)"},
      {.input = R"(C:\foo)", .expected = R"(C:\foo)"},
      {.input = "a/b/", .expected = R"(a\b)"},
      {.input = "../", .expected = ".."},
      {.input = "../../a/..", .expected = R"(..\..)"},
      {.input = "/users/..", .expected = R"(\)"},
      {.input = "C:/a/../../", .expected = R"(C:\)"},
      {.input = R"(\\server\share\\dir\)", .expected = R"(\\server\share\dir)"},
      {.input = "bad?/../ok", .expected = "ok"},
      {.input = "CON/../ok", .expected = "ok"},
      {.input = "bad. /../ok", .expected = "ok"},
      {.input = "a&b/../ok", .expected = "ok"},
  });
  for (const auto& fixture : k_fixtures)
  {
    DYNAMIC_SECTION(fixture.input)
    {
      checkPathConversion(fixture);
    }
  }
  const std::string k_long_name(300, 'x');
  checkPathConversion({.input = k_long_name, .expected = k_long_name});
}

TEST_CASE("A single sentence period is removed only from the final filename")
{
  constexpr auto k_fixtures = std::to_array<Fixture>({
      {.input = R"(d:\dir1\dir2\dir3.)", .expected = R"(D:\dir1\dir2\dir3)"},
      {.input = "d:/dir1/dir2/dir3.", .expected = R"(D:\dir1\dir2\dir3)"},
      {.input = "src/main.cpp.", .expected = R"(src\main.cpp)"},
      {.input = ".hidden.", .expected = ".hidden"},
      {.input = "name.", .expected = "name"},
      {.input = "a.", .expected = "a"},
      {.input = "/mnt/d/dir.", .expected = R"(D:\dir)"},
      {.input = "/d/dir.", .expected = R"(D:\dir)"},
      {.input = "/D:/dir.", .expected = R"(D:\dir)"},
      {.input = "///logs/dir.", .expected = R"(\logs\dir)"},
      {.input = R"(\\server\share\dir.)", .expected = R"(\\server\share\dir)"},
      {.input = "C:/my projects/app.", .expected = R"("C:\my projects\app")"},
      {.input = "C:/漢字/проект.", .expected = "C:\\漢字\\проект"},
      {.input = ".", .expected = "."},
      {.input = "..", .expected = ".."},
      {.input = "dir/.", .expected = "dir"},
      {.input = "dir/..", .expected = "."},
      {.input = "C:/.", .expected = R"(C:\)"},
      {.input = "name..", .expected = std::nullopt},
      {.input = "...", .expected = std::nullopt},
      {.input = "bad./file.", .expected = std::nullopt},
      {.input = "bad./", .expected = std::nullopt},
      {.input = "bad./.", .expected = std::nullopt},
      {.input = "name .", .expected = std::nullopt},
      {.input = "CON.", .expected = std::nullopt},
      {.input = "bad?.", .expected = std::nullopt},
      {.input = "C:dir.", .expected = std::nullopt},
      {.input = "./c:/dir.", .expected = std::nullopt},
      {.input = R"(\\server\share.)", .expected = R"(\\server\share.)"},
  });
  for (const auto& fixture : k_fixtures)
  {
    DYNAMIC_SECTION(fixture.input)
    {
      checkPathConversion(fixture);
    }
  }
  checkPathConversion({.input = "~/dir.", .expected = R"(C:\Users\me\dir)"},
                      R"(C:\Users\me)");
  checkPathConversion({.input = "~", .expected = std::nullopt},
                      R"(C:\Users\me.)");
}

TEST_CASE("Surviving filenames follow bounded Windows naming rules")
{
  constexpr auto k_fixtures = std::to_array<Fixture>({
      {.input = "C:/dir/con.txt", .expected = std::nullopt},
      {.input = "NUL.tar.gz", .expected = std::nullopt},
      {.input = "COM¹", .expected = std::nullopt},
      {.input = "LPT9.log", .expected = std::nullopt},
      {.input = "con", .expected = std::nullopt},
      {.input = "COM0", .expected = "COM0"},
      {.input = "COM10", .expected = "COM10"},
      {.input = "console.txt", .expected = "console.txt"},
      {.input = "C:/a/b:c", .expected = std::nullopt},
      {.input = "a/b<c", .expected = std::nullopt},
      {.input = "a/b?c", .expected = std::nullopt},
      {.input = "a/tab\tb", .expected = std::nullopt},
      {.input = "C:/dir/name.", .expected = R"(C:\dir\name)"},
      {.input = "C:/dir/name ", .expected = std::nullopt},
      {.input = ".hidden", .expected = ".hidden"},
      {.input = "../name", .expected = R"(..\name)"},
      {.input = " leading/x", .expected = R"(" leading\x")"},
      {.input = "PrN", .expected = std::nullopt},
      {.input = "aux.ext", .expected = std::nullopt},
      {.input = "a>b", .expected = std::nullopt},
      {.input = "a\"b", .expected = std::nullopt},
      {.input = "a|b", .expected = std::nullopt},
      {.input = "a*b", .expected = std::nullopt},
      {.input = "COM².txt", .expected = std::nullopt},
      {.input = "cOm³", .expected = std::nullopt},
      {.input = "LPT¹", .expected = std::nullopt},
      {.input = "lpt².ext", .expected = std::nullopt},
      {.input = "LPT³", .expected = std::nullopt},
      {.input = "LPT0", .expected = "LPT0"},
      {.input = "COM¹x", .expected = "COM¹x"},
      {.input = "NULx", .expected = "NULx"},
      {.input = ".CON", .expected = ".CON"},
  });
  for (const auto& fixture : k_fixtures)
  {
    DYNAMIC_SECTION(fixture.input)
    {
      checkPathConversion(fixture);
    }
  }
  for (char control = 0; control < 0x20; ++control)
  {
    DYNAMIC_SECTION("control byte " << static_cast<int>(control))
    {
      checkPathConversion({
          .input = std::string("a/b") + control + "c",
          .expected = std::nullopt,
      });
      checkPathConversion({
          .input = std::string("a/b") + control + "c/../ok",
          .expected = R"(a\ok)",
      });
      checkPathConversion({
          .input = std::string(R"(\\ser)") + control + R"(ver\share)",
          .expected = std::nullopt,
      });
      checkPathConversion({
          .input = std::string(R"(\\server\sh)") + control + "are",
          .expected = std::nullopt,
      });
    }
  }
  for (char digit = '1'; digit <= '9'; ++digit)
  {
    checkPathConversion(
        {.input = std::string("COM") + digit, .expected = std::nullopt});
    checkPathConversion({
        .input = std::string("lpt") + digit + ".txt",
        .expected = std::nullopt,
    });
  }
}

TEST_CASE("Final quoting uses only the specified trigger characters")
{
  constexpr auto k_fixtures = std::to_array<Fixture>({
      {.input = "C:/my projects/app", .expected = R"("C:\my projects\app")"},
      {.input = "a/b/c", .expected = R"(a\b\c)"},
      {
          .input = "C:/work/research&notes",
          .expected = R"("C:\work\research&notes")",
      },
      {.input = "C:/work/a^b", .expected = R"("C:\work\a^b")"},
      {.input = "C:/a(b)", .expected = R"path("C:\a(b)")path"},
      {.input = "C:/100%/x", .expected = R"(C:\100%\x)"},
      {.input = "C:/hi!", .expected = R"(C:\hi!)"},
      {
          .input = R"(\\server\my share\x)",
          .expected = R"("\\server\my share\x")",
      },
      {.input = "a)b", .expected = R"("a)b")"},
      {.input = "a(b", .expected = R"("a(b")"},
      {.input = R"("C:\foo")", .expected = std::nullopt},
  });
  for (const auto& fixture : k_fixtures)
  {
    DYNAMIC_SECTION(fixture.input)
    {
      checkPathConversion(fixture);
    }
  }
}

TEST_CASE("PowerShell results use forward slashes for every root kind")
{
  constexpr auto k_fixtures = std::to_array<Fixture>({
      {.input = "a/b/c", .expected = "a/b/c"},
      {.input = R"(src\main.cpp)", .expected = "src/main.cpp"},
      {.input = "../src", .expected = "../src"},
      {.input = "a/..", .expected = "."},
      {.input = ".", .expected = "."},
      {.input = "/users/me", .expected = "/users/me"},
      {.input = "/", .expected = "/"},
      {.input = "C:/", .expected = "C:/"},
      {.input = "c:/Foo/BAR/", .expected = "C:/Foo/BAR"},
      {.input = "/c", .expected = "C:/"},
      {.input = "/mnt/d/foo", .expected = "D:/foo"},
      {.input = "/C:/home/me", .expected = "C:/home/me"},
      {.input = R"(c:\users\kippesp\dir1\..)", .expected = "C:/users/kippesp"},
      {.input = R"(d:\dir1\dir2\dir3.)", .expected = "D:/dir1/dir2/dir3"},
      {.input = R"(\\server\share\dir)", .expected = "//server/share/dir"},
      {.input = R"(\\server\share\)", .expected = "//server/share/"},
      {.input = "//server/share", .expected = "//server/share"},
      {.input = R"(\\server\share\dir\..)", .expected = "//server/share/"},
      {.input = "smb://server/share/docs", .expected = "//server/share/docs"},
      {.input = "C:/Users/漢字/проект", .expected = "C:/Users/漢字/проект"},
      {.input = "C:/café/😀", .expected = "C:/café/😀"},
      {.input = "C:foo", .expected = std::nullopt},
      {.input = R"(\\server\share\..\other)", .expected = std::nullopt},
      {.input = "C:/dir/con.txt", .expected = std::nullopt},
      {.input = "name..", .expected = std::nullopt},
  });
  for (const auto& fixture : k_fixtures)
  {
    DYNAMIC_SECTION(fixture.input)
    {
      checkPathConversion(fixture, std::nullopt, np::PathStyle::kPowerShell);
    }
  }
  checkPathConversion({.input = "~/x", .expected = "C:/Users/me/x"},
                      R"(C:\Users\me)", np::PathStyle::kPowerShell);
  checkPathConversion({.input = "~", .expected = "'C:/Users/Jo Doe'"},
                      R"(C:\Users\Jo Doe)", np::PathStyle::kPowerShell);
  checkPathConversion({.input = "~/dir.", .expected = "C:/Users/Зоя/dir"},
                      R"(C:\Users\Зоя)", np::PathStyle::kPowerShell);
}

TEST_CASE("PowerShell quoting keeps the path text literal")
{
  constexpr auto k_fixtures = std::to_array<Fixture>({
      {.input = "C:/my projects/app", .expected = "'C:/my projects/app'"},
      {.input = "C:/my projects/app.", .expected = "'C:/my projects/app'"},
      {.input = " leading/x", .expected = "' leading/x'"},
      {
          .input = R"(\\server\my share\x)",
          .expected = "'//server/my share/x'",
      },
      {
          .input = "C:/Users/Paul's files",
          .expected = "'C:/Users/Paul''s files'",
      },
      {.input = "a'b'c", .expected = "'a''b''c'"},
      {.input = "C:/it’s", .expected = "'C:/it’’s'"},
      {.input = "‘a‚b‛c", .expected = "'‘‘a‚‚b‛‛c'"},
      {.input = "C:/q“x”„", .expected = "'C:/q“x”„'"},
      {.input = "//localhost/c$/Users", .expected = "'//localhost/c$/Users'"},
      {.input = "$HOME/x", .expected = "'$HOME/x'"},
      {
          .input = "C:/Program Files (x86)",
          .expected = "'C:/Program Files (x86)'",
      },
      {.input = "C:/a{b}", .expected = "'C:/a{b}'"},
      {.input = "a`b", .expected = "'a`b'"},
      {.input = R"(\\se"rv\share)", .expected = R"('//se"rv/share')"},
      {.input = R"(\\se|rv\share)", .expected = "'//se|rv/share'"},
      {.input = "./-dash", .expected = "'-dash'"},
      {.input = "#notes/x", .expected = "'#notes/x'"},
      {.input = "@types/node", .expected = "'@types/node'"},
      {.input = "./]x", .expected = "']x'"},
      {.input = "–x", .expected = "'–x'"},
      {.input = "—x", .expected = "'—x'"},
      {.input = "―x", .expected = "'―x'"},
      {.input = "a-b/C#/x@y/z]–", .expected = "a-b/C#/x@y/z]–"},
      {.input = "C:/work/a^b", .expected = "C:/work/a^b"},
      {.input = "C:/100%/x", .expected = "C:/100%/x"},
      {.input = "C:/hi!", .expected = "C:/hi!"},
      {.input = "a[1]/b=c+d~e", .expected = "a[1]/b=c+d~e"},
      {.input = "~user/x", .expected = "~user/x"},
      {.input = "1.10", .expected = "1.10"},
      {
          .input = "C:/zero\u200Bwidth\uFEFF",
          .expected = "C:/zero\u200Bwidth\uFEFF",
      },
  });
  for (const auto& fixture : k_fixtures)
  {
    DYNAMIC_SECTION(fixture.input)
    {
      checkPathConversion(fixture, std::nullopt, np::PathStyle::kPowerShell);
    }
  }
  constexpr std::string_view k_triggers = " $&'(),;`{}";
  for (char ascii = ' '; ascii <= '~'; ++ascii)
  {
    // Skip separators and characters rejected in filenames.
    if (std::string_view(R"(<>:"|?*/\)").contains(ascii))
      continue;
    const std::string k_path = std::string("a") + ascii + "b";
    std::string expected = k_path;
    if (k_triggers.contains(ascii))
      expected = ascii == '\'' ? "'a''b'" : "'" + k_path + "'";
    DYNAMIC_SECTION("ASCII " << ascii)
    {
      checkPathConversion({.input = k_path, .expected = expected}, std::nullopt,
                          np::PathStyle::kPowerShell);
    }
  }
  constexpr auto k_spaces = std::to_array<std::string_view>({
      "\u0085",
      "\u00A0",
      "\u1680",
      "\u2000",
      "\u2005",
      "\u200A",
      "\u2028",
      "\u2029",
      "\u202F",
      "\u205F",
      "\u3000",
  });
  for (const auto k_space : k_spaces)
  {
    const std::string k_path = std::string("C:/a") + std::string(k_space) + "b";
    DYNAMIC_SECTION("Unicode space " << k_path)
    {
      checkPathConversion({.input = k_path, .expected = "'" + k_path + "'"},
                          std::nullopt, np::PathStyle::kPowerShell);
    }
  }
}
