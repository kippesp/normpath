#pragma once

#include <cstdint>
#include <string_view>

namespace np {

enum class Shell : std::uint8_t { kUnknown, kCmd, kPowerShell };

// Classify a process image file name such as "pwsh.exe".
[[nodiscard]] Shell classifyShellImage(std::string_view a_image_name);
// Classify the parent process, which is the shell when np is typed at a
// prompt. Any failure to identify the parent yields Shell::kUnknown.
[[nodiscard]] Shell detectParentShell() noexcept;

}  // namespace np
