#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace np {

struct PathRejection {
  std::string_view reason;
};

[[nodiscard]] bool startsWithHomeToken(std::string_view a_text);
// Return the normalized Windows path with any required cmd.exe quoting.
[[nodiscard]] std::expected<std::string, PathRejection> convertToCmdPath(
    std::string_view a_text,
    std::optional<std::string_view> a_home = std::nullopt);

}  // namespace np
