#include "standard_streams.h"

#include <Windows.h>
#include <fcntl.h>
#include <io.h>

#include <cstddef>
#include <cstdio>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace np {
namespace {

bool isContinuation(unsigned char a_byte)
{
  return (a_byte & 0xC0U) == 0x80U;
}

struct Utf8Sequence {
  std::size_t length;
  unsigned int minimum_second = 0x80U;
  unsigned int maximum_second = 0xBFU;
};

std::optional<Utf8Sequence> sequenceFor(unsigned char a_lead)
{
  if (a_lead >= 0xC2U && a_lead <= 0xDFU)
    return Utf8Sequence {.length = 2};
  if (a_lead >= 0xE0U && a_lead <= 0xEFU)
  {
    return Utf8Sequence {
        .length = 3,
        .minimum_second = a_lead == 0xE0U ? 0xA0U : 0x80U,
        .maximum_second = a_lead == 0xEDU ? 0x9FU : 0xBFU,
    };
  }
  if (a_lead >= 0xF0U && a_lead <= 0xF4U)
  {
    return Utf8Sequence {
        .length = 4,
        .minimum_second = a_lead == 0xF0U ? 0x90U : 0x80U,
        .maximum_second = a_lead == 0xF4U ? 0x8FU : 0xBFU,
    };
  }
  return std::nullopt;
}

bool isValidUtf8(std::string_view a_text)
{
  for (std::size_t index = 0; index < a_text.size();)
  {
    const auto k_lead = static_cast<unsigned char>(a_text.at(index));
    if (k_lead < 0x80U)
    {
      ++index;
      continue;
    }

    const auto k_sequence = sequenceFor(k_lead);
    if (!k_sequence)
      return false;

    if (index + k_sequence->length > a_text.size())
      return false;
    const auto k_second = static_cast<unsigned char>(a_text.at(index + 1));
    if (k_second < k_sequence->minimum_second ||
        k_second > k_sequence->maximum_second)
      return false;
    for (std::size_t offset = 2; offset < k_sequence->length; ++offset)
    {
      if (!isContinuation(
              static_cast<unsigned char>(a_text.at(index + offset))))
        return false;
    }
    index += k_sequence->length;
  }
  return true;
}

std::expected<void, StreamFailure> setBinaryMode(std::FILE* a_stream)
{
  const int k_descriptor = _fileno(a_stream);
  if (k_descriptor < 0 || _setmode(k_descriptor, _O_BINARY) == -1)
    return std::unexpected(StreamFailure {"could not set binary stream mode"});
  return {};
}

bool isConsole(std::FILE* a_stream)
{
  const int k_descriptor = _fileno(a_stream);
  if (k_descriptor < 0)
    return false;
  // The CRT exposes the native HANDLE as an integer-valued descriptor.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
  auto* const k_handle = reinterpret_cast<HANDLE>(_get_osfhandle(k_descriptor));
  DWORD mode = 0;
  return GetConsoleMode(k_handle, &mode) != 0;
}

}  // namespace

std::optional<StdinRecord> StandardStreams::finishRecord(std::string a_text,
                                                         bool a_terminated)
{
  if (a_terminated && !a_text.empty() && a_text.back() == '\r')
    a_text.pop_back();
  if (at_stream_start_)
  {
    at_stream_start_ = false;
    if (a_text.starts_with("\xEF\xBB\xBF"))
      a_text.erase(0, 3);
  }
  if (!a_terminated && a_text.empty())
    return std::nullopt;
  const bool k_valid_utf8 = isValidUtf8(a_text);
  return StdinRecord {
      .text = std::move(a_text),
      .line = next_line_++,
      .valid_utf8 = k_valid_utf8,
  };
}

std::expected<std::optional<StdinRecord>, StreamFailure>
StandardStreams::nextRecord()
{
  if (input_state_ == InputState::kFailed)
    return std::unexpected(input_failure_);
  if (input_state_ == InputState::kFinished)
    return std::nullopt;
  if (input_state_ == InputState::kUnopened)
  {
    input_is_console_ = isConsole(input_);
    if (auto mode = setBinaryMode(input_); !mode)
    {
      input_failure_ = std::move(mode.error());
      input_state_ = InputState::kFailed;
      return std::unexpected(input_failure_);
    }
    input_state_ = InputState::kReady;
  }

  std::string text;
  while (true)
  {
    const int k_byte = std::fgetc(input_);
    if (k_byte == EOF)
    {
      if (std::ferror(input_) != 0)
      {
        input_failure_ = StreamFailure {"standard input read failed"};
        input_state_ = InputState::kFailed;
        return std::unexpected(input_failure_);
      }
      input_state_ = InputState::kFinished;
      if (text.empty())
        return std::nullopt;
    }
    else if (k_byte != '\n')
    {
      text.push_back(static_cast<char>(k_byte));
      continue;
    }

    // A console sends Ctrl+Z followed by Enter as a line. Redirected 0x1A
    // remains ordinary input, including when it is the entire record.
    if (input_is_console_ && (text == "\x1A\r" || text == "\x1A"))
    {
      input_state_ = InputState::kFinished;
      return std::nullopt;
    }
    return finishRecord(std::move(text), k_byte == '\n');
  }
}

std::expected<void, StreamFailure> StandardStreams::write(
    std::FILE* a_stream, bool& a_mode_ready, std::string_view a_text)
{
  if (!a_mode_ready)
  {
    if (auto mode = setBinaryMode(a_stream); !mode)
      return mode;
    a_mode_ready = true;
  }
  if (!a_text.empty())
  {
    // Keep a UTF-8 record together when an unbuffered CRT stream targets a
    // console.
#ifdef __clang__
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-libc-call"
#endif
    const std::size_t k_written =
        std::fwrite(a_text.data(), 1, a_text.size(), a_stream);
#ifdef __clang__
  #pragma clang diagnostic pop
#endif
    // The UCRT can report a full fwrite count on an unbuffered stream even
    // when the underlying write sets the stream error indicator.
    if (k_written != a_text.size() || std::ferror(a_stream) != 0)
      return std::unexpected(StreamFailure {"standard stream write failed"});
  }
  if (std::fflush(a_stream) != 0)
    return std::unexpected(StreamFailure {"standard stream flush failed"});
  return {};
}

std::expected<void, StreamFailure> StandardStreams::writeStdout(
    std::string_view a_text)
{
  return write(output_, output_ready_, a_text);
}

std::expected<void, StreamFailure> StandardStreams::writeStderr(
    std::string_view a_text)
{
  return write(error_, error_ready_, a_text);
}

}  // namespace np
