#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace np {

struct PathRejection {
  std::string_view reason;
};

// Separator and quoting conventions for the shell that receives the result.
enum class PathStyle : std::uint8_t { kCmd, kPowerShell };

[[nodiscard]] bool startsWithHomeToken(std::string_view a_text);
// Return the normalized Windows path with the separators and any quoting
// required by a_style.
[[nodiscard]] std::expected<std::string, PathRejection> convertPath(
    std::string_view a_text, PathStyle a_style,
    std::optional<std::string_view> a_home = std::nullopt);

}  // namespace np
