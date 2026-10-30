#include "path_conversion.h"
#include "shell_detection.h"
#include "standard_streams.h"
#include "version.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kHelp =
    "np - Path Normalizer for Windows\n\n"
    "Converts path spellings to Windows form. Run from PowerShell, np uses\n"
    "forward slashes and PowerShell quoting; otherwise, including cmd.exe,\n"
    "it uses backslashes and cmd.exe quoting.\n\n"
    "USAGE: np [options] [PATH...]\n\n"
    "ARGUMENTS:\n"
    "  PATH...    Paths to convert; - reads one path per line from stdin\n\n"
    "OPTIONS:\n"
    "  -h, --help     Print help\n"
    "      --version  Print version and build information\n";

enum class Action : std::uint8_t { kConvert, kVersion, kHelp };

enum class ExitCode : std::uint8_t {
  kSuccess = 0,
  kInputRejected = 1,
  kFatalError = 2,
};

struct Invocation {
  Action action = Action::kConvert;
  std::vector<std::string_view> operands;
};

std::expected<Invocation, std::string_view> parseInvocation(
    std::span<char*> a_arguments)
{
  Invocation invocation;
  if (a_arguments.empty())
    invocation.action = Action::kHelp;
  bool options = true;
  for (const std::string_view k_argument : a_arguments)
  {
    if (options && k_argument == "--")
    {
      options = false;
    }
    else if (options && (k_argument == "-h" || k_argument == "--help"))
    {
      invocation.action = Action::kHelp;
    }
    else if (options && k_argument == "--version")
    {
      if (invocation.action != Action::kHelp)
        invocation.action = Action::kVersion;
    }
    else if (options && k_argument.starts_with('-') && k_argument != "-")
    {
      return std::unexpected("Unknown option; see --help");
    }
    else
    {
      invocation.operands.push_back(k_argument);
    }
  }
  if (invocation.action == Action::kConvert && invocation.operands.empty())
    return std::unexpected("No path provided; see --help");
  return invocation;
}

// A failed stderr attempt is terminal. Formatting failures before the attempt
// propagate to the fixed-text unexpected-failure handler.
struct DiagnosticFailure : std::exception {};

class StreamException : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

void writeDiagnostic(np::StandardStreams& a_streams, std::string_view a_reason)
{
  const auto k_diagnostic = std::format("np: error: {}\n", a_reason);
  bool delivered = false;
  try
  {
    delivered = a_streams.writeStderr(k_diagnostic).has_value();
  }
  catch (...)
  {
    delivered = false;
  }
  if (!delivered)
    throw DiagnosticFailure {};
}

void reportUnexpectedFailure(np::StandardStreams& a_streams) noexcept
{
  try
  {
    // This last attempt must work without allocating or formatting a message.
    if (!a_streams.writeStderr("np: error: unexpected failure\n"))
      return;
  }
  catch (...)
  {
    return;
  }
}

void writeOutput(np::StandardStreams& a_streams, std::string_view a_text)
{
  if (auto delivery = a_streams.writeStdout(a_text); !delivery)
    throw StreamException(delivery.error().reason);
}

std::optional<std::string> readHome()
{
  char* home = nullptr;
  std::size_t size = 0;
  const int k_error = _dupenv_s(&home, &size, "USERPROFILE");
  const std::unique_ptr<char, decltype(&std::free)> k_owned(home, &std::free);
  if (k_error != 0)
    throw std::runtime_error("could not read USERPROFILE");
  if (k_owned == nullptr)
    return std::nullopt;
  return std::string(k_owned.get());
}

// The path and its formatted location are explicit at each call site.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool convertInput(np::StandardStreams& a_streams, std::string_view a_text,
                  std::string_view a_location, np::PathStyle a_style)
{
  const auto k_home =
      np::startsWithHomeToken(a_text) ? readHome() : std::nullopt;
  auto result = np::convertPath(
      a_text, a_style,
      k_home ? std::optional<std::string_view>(*k_home) : std::nullopt);
  if (!result)
  {
    writeDiagnostic(a_streams,
                    std::format("{}: {}", a_location, result.error().reason));
    return false;
  }
  result->push_back('\n');
  writeOutput(a_streams, *result);
  return true;
}

ExitCode run(np::StandardStreams& a_streams, std::span<char*> a_arguments)
{
  const auto k_invocation = parseInvocation(a_arguments);
  if (!k_invocation)
  {
    writeDiagnostic(a_streams, k_invocation.error());
    return ExitCode::kFatalError;
  }
  if (k_invocation->action == Action::kHelp)
  {
    writeOutput(a_streams, kHelp);
    return ExitCode::kSuccess;
  }
  if (k_invocation->action == Action::kVersion)
  {
    writeOutput(a_streams, std::format("np - Path Normalizer for Windows "
                                       "(ver. {} - {})\n"
                                       "https://github.com/kippesp/normpath\n",
                                       np::kVersion, np::kBuildRevision));
    return ExitCode::kSuccess;
  }

  // An unrecognized parent, such as a script host or an exited launcher,
  // keeps the cmd.exe spelling np produced before shell detection.
  const auto k_style = np::detectParentShell() == np::Shell::kPowerShell
                           ? np::PathStyle::kPowerShell
                           : np::PathStyle::kCmd;
  bool rejected = false;
  std::size_t position = 0;
  for (const auto k_operand : k_invocation->operands)
  {
    ++position;
    if (k_operand != "-")
    {
      if (!convertInput(a_streams, k_operand,
                        std::format("operand {}", position), k_style))
        rejected = true;
      continue;
    }
    while (true)
    {
      const auto k_read = a_streams.nextRecord();
      if (!k_read)
        throw StreamException(k_read.error().reason);
      if (!*k_read)
        break;
      const auto& k_record = k_read->value();
      const auto k_location = std::format("stdin line {}", k_record.line);
      if (!k_record.valid_utf8)
      {
        writeDiagnostic(a_streams,
                        std::format("{}: ill-formed UTF-8", k_location));
        rejected = true;
      }
      else if (!convertInput(a_streams, k_record.text, k_location, k_style))
      {
        rejected = true;
      }
    }
  }
  return rejected ? ExitCode::kInputRejected : ExitCode::kSuccess;
}

}  // namespace

int main(int a_argc, char* a_argv[])
{
  if (a_argc == 0 || a_argv == nullptr || a_argv[0] == nullptr)
    return static_cast<int>(ExitCode::kSuccess);

  np::StandardStreams streams(stdin, stdout, stderr);
  try
  {
#ifdef __clang__
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
    const std::span k_arguments(a_argv, static_cast<std::size_t>(a_argc));
#ifdef __clang__
  #pragma clang diagnostic pop
#endif
    return static_cast<int>(run(streams, k_arguments.subspan(1)));
  }
  catch (const DiagnosticFailure&)
  {
    return static_cast<int>(ExitCode::kFatalError);
  }
  catch (const StreamException& a_failure)
  {
    try
    {
      writeDiagnostic(streams, a_failure.what());
    }
    catch (const DiagnosticFailure&)
    {
      return static_cast<int>(ExitCode::kFatalError);
    }
    catch (...)
    {
      reportUnexpectedFailure(streams);
    }
  }
  catch (...)
  {
    reportUnexpectedFailure(streams);
  }
  return static_cast<int>(ExitCode::kFatalError);
}
