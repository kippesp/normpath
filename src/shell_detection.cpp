#include "shell_detection.h"

#include <Windows.h>
// TlHelp32.h uses Windows.h declarations without including them.
#include <TlHelp32.h>

#include <algorithm>
#include <optional>
#include <string_view>

namespace np {
namespace {

bool equalsIgnoringAsciiCase(std::string_view a_left, std::string_view a_right)
{
  return std::ranges::equal(a_left, a_right, [](char a_lhs, char a_rhs) {
    const auto k_lower = [](char a_char) {
      return a_char >= 'A' && a_char <= 'Z'
                 ? static_cast<char>(a_char - 'A' + 'a')
                 : a_char;
    };
    return k_lower(a_lhs) == k_lower(a_rhs);
  });
}

// One snapshot supplies both this process's parent ID and the parent's image
// name, so the parent cannot be looked up in a different process list.
std::optional<Shell> classifySnapshotParent(HANDLE a_snapshot)
{
  const DWORD k_self = GetCurrentProcessId();
  PROCESSENTRY32 entry {};
  entry.dwSize = sizeof(entry);
  std::optional<DWORD> parent;
  for (BOOL more = Process32First(a_snapshot, &entry); more != FALSE;
       more = Process32Next(a_snapshot, &entry))
  {
    if (entry.th32ProcessID == k_self)
    {
      parent = entry.th32ParentProcessID;
      break;
    }
  }
  if (!parent)
    return std::nullopt;
  for (BOOL more = Process32First(a_snapshot, &entry); more != FALSE;
       more = Process32Next(a_snapshot, &entry))
  {
    if (entry.th32ProcessID == *parent)
      return classifyShellImage(entry.szExeFile);
  }
  return std::nullopt;
}

}  // namespace

Shell classifyShellImage(std::string_view a_image_name)
{
  if (equalsIgnoringAsciiCase(a_image_name, "cmd.exe"))
    return Shell::kCmd;
  if (equalsIgnoringAsciiCase(a_image_name, "pwsh.exe") ||
      equalsIgnoringAsciiCase(a_image_name, "powershell.exe"))
  {
    return Shell::kPowerShell;
  }
  return Shell::kUnknown;
}

Shell detectParentShell() noexcept
{
  auto* const k_snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (k_snapshot == INVALID_HANDLE_VALUE)
    return Shell::kUnknown;
  const auto k_shell = classifySnapshotParent(k_snapshot);
  CloseHandle(k_snapshot);
  return k_shell.value_or(Shell::kUnknown);
}

}  // namespace np
