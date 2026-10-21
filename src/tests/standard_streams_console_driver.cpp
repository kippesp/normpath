#include "standard_streams.h"

#include <Windows.h>
#include <io.h>
#include <process.h>

#include <array>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace {

int standardHandlesAreConsoles()
{
  int index = 0;
  for (FILE* const k_stream : {stdin, stdout, stderr})
  {
    DWORD mode = 0;
    auto* const k_handle =
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
        reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(k_stream)));
    if (GetConsoleMode(k_handle, &mode) == 0)
      return 30 + index;
    ++index;
  }
  return 0;
}

int prepareConsole()
{
  // ConPTY gives this test process a console but CTest may supply inherited
  // non-console CRT descriptors. Bind the borrowed streams to console devices.
  FILE* rebound = nullptr;
  if (freopen_s(&rebound, "CONIN$", "r+b", stdin) != 0 || rebound == nullptr)
    return 23;
  if (freopen_s(&rebound, "CONOUT$", "w+b", stdout) != 0 || rebound == nullptr)
    return 24;
  if (freopen_s(&rebound, "CONOUT$", "w+b", stderr) != 0 || rebound == nullptr)
    return 25;
  if (const int k_handle_error = standardHandlesAreConsoles())
    return k_handle_error;
  if (SetConsoleCP(CP_UTF8) == 0 || SetConsoleOutputCP(CP_UTF8) == 0 ||
      GetConsoleCP() != CP_UTF8 || GetConsoleOutputCP() != CP_UTF8)
    return 20;
  DWORD input_mode = 0;
  HANDLE k_input = GetStdHandle(STD_INPUT_HANDLE);
  if (GetConsoleMode(k_input, &input_mode) == 0 ||
      SetConsoleMode(k_input,
                     input_mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT)) == 0)
    return 21;

  np::StandardStreams streams(stdin, stdout, stderr);
  if (!streams.writeStdout("READY\n"))
    return 22;
  return 0;
}

int runConsoleProbe()
{
  if (const int k_setup_error = prepareConsole())
    return k_setup_error;
  np::StandardStreams streams(stdin, stdout, stderr);
  const auto k_record = streams.nextRecord();
  if (!k_record)
    return 12;
  if (!*k_record || !k_record->value().valid_utf8 ||
      k_record->value().line != 1 || k_record->value().text != "café")
    return 13;
  if (!streams.writeStdout("IN:café\n"))
    return 14;
  if (!streams.writeStdout("OUT:é漢😀\n"))
    return 15;
  if (!streams.writeStderr("ERR:ñ界🚀\n"))
    return 16;

  for (int repeat = 0; repeat < 2; ++repeat)
  {
    const auto k_end = streams.nextRecord();
    if (!k_end || *k_end)
      return 17;
  }
  return streams.writeStdout("EOF2\n") ? 0 : 18;
}

int runInteractiveDriver()
{
  np::StandardStreams streams(stdin, stdout, stderr);
  while (true)
  {
    auto record = streams.nextRecord();
    if (!record)
      return 2;
    if (!*record)
      break;
    if (!record->value().valid_utf8)
      return 1;
    std::string line = std::move(record->value().text);
    line.push_back('\n');
    if (!streams.writeStdout(line))
      return 2;
  }
  return streams.writeStdout("[EOF]\n") ? 0 : 2;
}

}  // namespace

int main(int a_argc, char* a_argv[])
{
  try
  {
#ifdef __clang__
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
    const std::span k_args(a_argv, static_cast<std::size_t>(a_argc));
#ifdef __clang__
  #pragma clang diagnostic pop
#endif
    if (k_args.size() == 2 &&
        std::string_view(k_args.back()) == "--console-probe")
      return runConsoleProbe();
    if (k_args.size() == 3 &&
        std::string_view(k_args.subspan(1).front()) == "--cli-console")
    {
      if (const int k_setup_error = prepareConsole())
        return k_setup_error;
      // Only the launcher configures the isolated console. The product inherits
      // real console streams and runs its ordinary CLI and reader.
      // The CRT joins spawn arguments without quoting, including argv[0].
      const std::string k_child_name = "\"" + std::string(k_args.back()) + "\"";
      const std::array<const char*, 5> k_child_args = {
          k_child_name.c_str(), "-", "after/x", "-", nullptr,
      };
      const auto k_status =
          _spawnv(_P_WAIT, k_args.back(), k_child_args.data());
      if (k_status != 0)
        return 26;
      if (GetConsoleCP() != CP_UTF8 || GetConsoleOutputCP() != CP_UTF8)
        return 27;
      np::StandardStreams streams(stdin, stdout, stderr);
      return streams.writeStdout("CLI_DONE\n") ? 0 : 28;
    }
    return runInteractiveDriver();
  }
  catch (...)
  {
    return 19;
  }
}
