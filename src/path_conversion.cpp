#include "path_conversion.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace np {
namespace {

enum class RootKind : std::uint8_t { kRelative, kRooted, kDrive, kUnc };

struct Root {
  RootKind kind = RootKind::kRelative;
  char drive = '\0';
  std::string_view server;
  std::string_view share;
  std::size_t filenames = 0;
};

bool isPathSeparator(char a_char)
{
  return a_char == '/' || a_char == '\\';
}

char toAsciiUpper(char a_char)
{
  return a_char >= 'a' && a_char <= 'z' ? static_cast<char>(a_char - 'a' + 'A')
                                        : a_char;
}

bool isDriveLetter(char a_char)
{
  return toAsciiUpper(a_char) >= 'A' && toAsciiUpper(a_char) <= 'Z';
}

bool startsWithSmbScheme(std::string_view a_text)
{
  return a_text.size() >= 4 && toAsciiUpper(a_text.at(0)) == 'S' &&
         toAsciiUpper(a_text.at(1)) == 'M' &&
         toAsciiUpper(a_text.at(2)) == 'B' && a_text.at(3) == ':';
}

std::optional<PathRejection> validateSmbUrl(std::string_view a_text)
{
  if (!a_text.substr(4).starts_with("//") || a_text.contains('\\'))
    return PathRejection {"SMB URL requires smb://server/share"};
  if (a_text.contains('%'))
    return PathRejection {"percent signs are unsupported in SMB URLs"};
  if (a_text.find_first_of("?#") != std::string_view::npos)
    return PathRejection {"queries and fragments are unsupported in SMB URLs"};
  const auto k_server_end = a_text.find('/', 6);
  if (k_server_end == std::string_view::npos || k_server_end == 6 ||
      k_server_end + 1 == a_text.size() || a_text.at(k_server_end + 1) == '/')
    return PathRejection {"SMB URL requires a server and share"};
  const auto k_server = a_text.substr(6, k_server_end - 6);
  if (k_server.find_first_of("@:[]") != std::string_view::npos)
  {
    return PathRejection {
        "credentials, ports and IPv6 are unsupported in SMB URLs",
    };
  }
  return std::nullopt;
}

// C0 controls are bytes 0x00-0x1F; DEL (0x7F) is outside this set.
bool containsC0Control(std::string_view a_text)
{
  return std::ranges::any_of(a_text, [](char a_char) {
    return static_cast<unsigned char>(a_char) < 0x20;
  });
}

std::expected<Root, PathRejection> parsePathRoot(std::string_view a_text)
{
  if (a_text.empty())
    return std::unexpected(PathRejection {"empty path"});

  if (a_text.size() >= 2 && isDriveLetter(a_text.at(0)) && a_text.at(1) == ':')
  {
    if (a_text.size() < 3 || !isPathSeparator(a_text.at(2)))
      return std::unexpected(PathRejection {"drive-relative path"});
    return Root {
        .kind = RootKind::kDrive,
        .drive = toAsciiUpper(a_text.at(0)),
        .server = {},
        .share = {},
        .filenames = 3,
    };
  }

  const auto k_leading = a_text.find_first_not_of("/\\");
  if (k_leading == std::string_view::npos && a_text.size() >= 2)
    return std::unexpected(PathRejection {"incomplete UNC root"});
  if (k_leading == 2)
  {
    const auto k_server_end = a_text.find_first_of("/\\", k_leading);
    const auto k_server = a_text.substr(k_leading, k_server_end - k_leading);
    if (k_server == "?" || k_server == ".")
      return std::unexpected(PathRejection {"Windows namespace path"});
    if (k_server_end == std::string_view::npos)
      return std::unexpected(PathRejection {"incomplete UNC root"});
    const auto k_share_begin = a_text.find_first_not_of("/\\", k_server_end);
    if (k_share_begin == std::string_view::npos)
      return std::unexpected(PathRejection {"incomplete UNC root"});
    const auto k_share_end = a_text.find_first_of("/\\", k_share_begin);
    const auto k_share =
        a_text.substr(k_share_begin, k_share_end - k_share_begin);
    if (containsC0Control(k_server) || containsC0Control(k_share))
      return std::unexpected(PathRejection {"control character in UNC root"});
    return Root {
        .kind = RootKind::kUnc,
        .drive = '\0',
        .server = k_server,
        .share = k_share,
        .filenames =
            k_share_end == std::string_view::npos ? a_text.size() : k_share_end,
    };
  }
  if (isPathSeparator(a_text.at(0)))
  {
    return Root {
        .kind = RootKind::kRooted,
        .drive = '\0',
        .server = {},
        .share = {},
        .filenames =
            k_leading == std::string_view::npos ? a_text.size() : k_leading,
    };
  }
  return Root {};
}

// Mounts are input notation only. Never reinterpret normalized Windows text.
std::optional<std::size_t> findMountDriveLetter(std::string_view a_text)
{
  if (!a_text.starts_with('/') || a_text.size() < 2 ||
      isPathSeparator(a_text.at(1)))
  {
    return std::nullopt;
  }
  const std::size_t k_drive = a_text.size() >= 6 &&
                                      a_text.substr(1, 3) == "mnt" &&
                                      isPathSeparator(a_text.at(4))
                                  ? 5
                                  : 1;
  if (!isDriveLetter(a_text.at(k_drive)) ||
      (a_text.size() > k_drive + 1 && !isPathSeparator(a_text.at(k_drive + 1))))
  {
    return std::nullopt;
  }
  return k_drive;
}

std::string interpretDriveNotation(std::string a_text)
{
  // Interpret this input notation before normalization can cancel its
  // designator.
  if (a_text.size() >= 3 && a_text.at(0) == '/' &&
      isDriveLetter(a_text.at(1)) && a_text.at(2) == ':')
  {
    a_text.erase(0, 1);
    if (a_text.size() == 2)
      a_text += '\\';
  }
  else if (const auto k_drive = findMountDriveLetter(a_text))
  {
    const auto k_tail = a_text.substr(*k_drive + 1);
    a_text = std::string(1, toAsciiUpper(a_text.at(*k_drive))) + ":" +
             (k_tail.empty() ? "\\" : k_tail);
  }
  return a_text;
}

bool haveSameRootIdentity(const Root& a_before, const Root& a_after)
{
  return a_before.kind == a_after.kind && a_before.drive == a_after.drive &&
         a_before.server == a_after.server && a_before.share == a_after.share;
}

bool isReservedDeviceName(std::string_view a_component)
{
  const auto k_stem = a_component.substr(0, a_component.find('.'));
  std::string upper(k_stem);
  std::ranges::transform(upper, upper.begin(), toAsciiUpper);
  if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL")
    return true;
  if (!upper.starts_with("COM") && !upper.starts_with("LPT"))
    return false;
  const auto k_suffix = std::string_view(upper).substr(3);
  return (k_suffix.size() == 1 && k_suffix.at(0) >= '1' &&
          k_suffix.at(0) <= '9') ||
         k_suffix == "¹" || k_suffix == "²" || k_suffix == "³";
}

std::optional<PathRejection> validateFilenameComponents(std::string_view a_text,
                                                        std::size_t a_begin)
{
  while (a_begin < a_text.size())
  {
    const auto k_end = a_text.find('\\', a_begin);
    const auto k_component = a_text.substr(a_begin, k_end - a_begin);
    if (!k_component.empty() && k_component != "." && k_component != "..")
    {
      if (containsC0Control(k_component) ||
          k_component.find_first_of("<>:\"|?*") != std::string_view::npos)
      {
        return PathRejection {"prohibited character in filename"};
      }
      if (isReservedDeviceName(k_component))
        return PathRejection {"reserved device name"};
      if (k_component.back() == ' ' || k_component.back() == '.')
        return PathRejection {"filename ends in a space or period"};
    }
    if (k_end == std::string_view::npos)
      break;
    a_begin = k_end + 1;
  }
  return std::nullopt;
}

}  // namespace

bool startsWithHomeToken(std::string_view a_text)
{
  return a_text == "~" || (a_text.size() >= 2 && a_text.at(0) == '~' &&
                           isPathSeparator(a_text.at(1)));
}

std::expected<std::string, PathRejection> convertToCmdPath(
    std::string_view a_text, std::optional<std::string_view> a_home)
{
  std::string interpreted;
  if (startsWithHomeToken(a_text))
  {
    if (!a_home || a_home->empty())
      return std::unexpected(PathRejection {"home value is absent or empty"});
    interpreted = *a_home;
    interpreted += a_text.substr(1);
  }
  else
  {
    interpreted = a_text;
  }

  if (startsWithSmbScheme(interpreted))
  {
    if (const auto k_rejection = validateSmbUrl(interpreted))
      return std::unexpected(*k_rejection);
    interpreted.erase(0, 4);
  }

  interpreted = interpretDriveNotation(std::move(interpreted));
  const auto k_before = parsePathRoot(interpreted);
  if (!k_before)
    return std::unexpected(k_before.error());

  // Collapse extra leading separators in drive-unspecified rooted input.
  std::string windows_text = interpreted;
  std::ranges::replace(windows_text, '/', '\\');
  if (k_before->kind == RootKind::kRooted)
    windows_text.erase(0, k_before->filenames - 1);

  auto normalized =
      std::filesystem::path(windows_text).lexically_normal().string();
  if (normalized.size() >= 2 && isDriveLetter(normalized.at(0)) &&
      normalized.at(1) == ':')
  {
    normalized.at(0) = toAsciiUpper(normalized.at(0));
  }
  const auto k_after = parsePathRoot(normalized);
  if (!k_after || !haveSameRootIdentity(*k_before, *k_after))
  {
    return std::unexpected(
        PathRejection {"normalization changed the path root"});
  }

  if (normalized.size() > k_after->filenames && normalized.back() == '\\' &&
      (k_after->kind != RootKind::kUnc ||
       normalized.size() != k_after->filenames + 1))
  {
    normalized.pop_back();
  }
  if (const auto k_rejection =
          validateFilenameComponents(normalized, k_after->filenames))
  {
    return std::unexpected(*k_rejection);
  }

  if (normalized.find_first_of(" &()^") != std::string::npos)
    normalized = '"' + normalized + '"';
  return normalized;
}

}  // namespace np
