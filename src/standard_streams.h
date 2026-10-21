#pragma once

#include <cstdio>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace np {

#if defined(__clang__)
  #define NP_STREAM_LIFETIME_BOUND [[clang::lifetimebound]]
#else
  #define NP_STREAM_LIFETIME_BOUND
#endif

struct StdinRecord {
  std::string text;
  std::size_t line;
  bool valid_utf8;
};

struct StreamFailure {
  std::string reason;
};

class StandardStreams {
public:
  StandardStreams(std::FILE* a_input NP_STREAM_LIFETIME_BOUND,
                  std::FILE* a_output NP_STREAM_LIFETIME_BOUND,
                  std::FILE* a_error NP_STREAM_LIFETIME_BOUND)
      : input_(a_input), output_(a_output), error_(a_error)
  {
  }

  std::expected<std::optional<StdinRecord>, StreamFailure> nextRecord();
  std::expected<void, StreamFailure> writeStdout(std::string_view a_text);
  std::expected<void, StreamFailure> writeStderr(std::string_view a_text);

private:
  enum class InputState { kUnopened, kReady, kFinished, kFailed };

  std::optional<StdinRecord> finishRecord(std::string a_text,
                                          bool a_terminated);
  static std::expected<void, StreamFailure> write(std::FILE* a_stream,
                                                  bool& a_mode_ready,
                                                  std::string_view a_text);

  std::FILE* input_;
  std::FILE* output_;
  std::FILE* error_;
  std::size_t next_line_ = 1;
  InputState input_state_ = InputState::kUnopened;
  StreamFailure input_failure_;
  bool input_is_console_ = false;
  bool output_ready_ = false;
  bool error_ready_ = false;
  bool at_stream_start_ = true;
};

}  // namespace np

#undef NP_STREAM_LIFETIME_BOUND
