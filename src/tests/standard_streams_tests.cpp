#include "standard_streams.h"

#include <Windows.h>
#include <fcntl.h>
#include <io.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include <catch2/catch_test_macros.hpp>

// Catch's REQUIRE does not expose its control flow to these analyses.
// NOLINTBEGIN(bugprone-unchecked-optional-access,readability-function-cognitive-complexity)

namespace {

struct CloseFile {
  void operator()(std::FILE* a_file) const
  {
    if (a_file != nullptr)
    {
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cert-err33-c)
      (void)std::fclose(a_file);
    }
  }
};

struct CloseWinHandle {
  void operator()(void* a_handle) const
  {
    if (a_handle != nullptr)
      (void)CloseHandle(a_handle);
  }
};

using File = std::unique_ptr<std::FILE, CloseFile>;
using WinHandle = std::unique_ptr<void, CloseWinHandle>;

File makeScratch()
{
  std::FILE* raw_file = nullptr;
  REQUIRE(tmpfile_s(&raw_file) == 0);
  return File(raw_file);
}

File makeInput(std::string_view a_bytes)
{
  File file = makeScratch();
  for (const char k_byte : a_bytes)
    REQUIRE(std::fputc(static_cast<unsigned char>(k_byte), file.get()) != EOF);
  REQUIRE(std::fflush(file.get()) == 0);
  REQUIRE(std::fseek(file.get(), 0, SEEK_SET) == 0);
  return file;
}

std::string readAll(std::FILE* a_file)
{
  std::string bytes;
  for (int byte = std::fgetc(a_file); byte != EOF; byte = std::fgetc(a_file))
    bytes.push_back(static_cast<char>(byte));
  return bytes;
}

void checkRecords(std::string_view a_input,
                  std::initializer_list<std::string_view> a_expected)
{
  const File k_input = makeInput(a_input);
  const File k_output = makeScratch();
  const File k_error = makeScratch();
  np::StandardStreams streams(k_input.get(), k_output.get(), k_error.get());
  std::size_t line = 1;
  for (const auto k_expected : a_expected)
  {
    const auto k_result = streams.nextRecord();
    REQUIRE(k_result.has_value());
    REQUIRE(k_result->has_value());
    CHECK(k_result->value().text == k_expected);
    CHECK(k_result->value().line == line++);
    CHECK(k_result->value().valid_utf8);
  }
  for (int repeat = 0; repeat < 2; ++repeat)
  {
    const auto k_result = streams.nextRecord();
    REQUIRE(k_result.has_value());
    CHECK(!k_result->has_value());
  }
}

}  // namespace

TEST_CASE("Input framing preserves bytes, blank records, and physical lines")
{
  checkRecords("", {});
  checkRecords("\n", {""});
  checkRecords("a\nb\r\n\nlast\r", {"a", "b", "", "last\r"});
  checkRecords("a\r\r\n", {"a\r"});
  checkRecords("\x1A\n", {"\x1A"});
  checkRecords(" space \"quote\"\x1A\n", {" space \"quote\"\x1A"});
}

TEST_CASE("Only the initial UTF-8 BOM is removed")
{
  checkRecords("\xEF\xBB\xBF\n\xEF\xBB\xBFpath\n", {"", "\xEF\xBB\xBFpath"});
  checkRecords("x\xEF\xBB\xBF\n", {"x\xEF\xBB\xBF"});
  checkRecords("\xEF\xBB\xBF", {});
}

TEST_CASE("Valid UTF-8 boundary sequences reach conversion")
{
  checkRecords(
      "\xC2\x80\n\xDF\xBF\n\xE0\xA0\x80\n"
      "\xED\x9F\xBF\n\xEE\x80\x80\n\xF0\x90\x80\x80\n"
      "\xF4\x8F\xBF\xBF",
      {
          "\xC2\x80",
          "\xDF\xBF",
          "\xE0\xA0\x80",
          "\xED\x9F\xBF",
          "\xEE\x80\x80",
          "\xF0\x90\x80\x80",
          "\xF4\x8F\xBF\xBF",
      });
}

TEST_CASE("Embedded NUL is encoding-valid content")
{
  constexpr char k_bytes[] = {'a', '\0', 'b', '\n'};
  const File k_input = makeInput(std::string_view(k_bytes, sizeof(k_bytes)));
  const File k_output = makeScratch();
  const File k_error = makeScratch();
  np::StandardStreams streams(k_input.get(), k_output.get(), k_error.get());
  auto record = streams.nextRecord();
  REQUIRE(record.has_value());
  REQUIRE(record->has_value());
  CHECK(record->value().text == std::string_view(k_bytes, 3));
  CHECK(record->value().valid_utf8);
}

TEST_CASE("Malformed UTF-8 is a record rejection and later lines continue")
{
  constexpr auto k_invalid = std::to_array<std::string_view>({
      "\x80",
      "\xC0\x80",
      "\xC1\xBF",
      "\xE0\x9F\xBF",
      "\xED\xA0\x80",
      "\xF0\x8F\xBF\xBF",
      "\xF4\x90\x80\x80",
      "\xF5\x80\x80\x80",
      "\xC2",
      "\xE2\x82",
      "\xF0\x90\x80",
      "\xE2\x28\xA1",
      "\xE2\x82\x41",
      "\xF0\x90\x41\x80",
      "\xF0\x90\x80\x41",
  });
  for (const auto k_invalid_text : k_invalid)
  {
    DYNAMIC_SECTION(k_invalid_text)
    {
      std::string bytes = "ok\n";
      bytes.append(k_invalid_text);
      bytes.append("\nok\n");
      const File k_input = makeInput(bytes);
      const File k_output = makeScratch();
      const File k_error = makeScratch();
      np::StandardStreams streams(k_input.get(), k_output.get(), k_error.get());
      for (const bool k_expected_valid : {true, false, true})
      {
        auto record = streams.nextRecord();
        REQUIRE(record.has_value());
        REQUIRE(record->has_value());
        CHECK(record->value().valid_utf8 == k_expected_valid);
      }
      auto end = streams.nextRecord();
      REQUIRE(end.has_value());
      CHECK(!end->has_value());
    }
    DYNAMIC_SECTION("unterminated " << k_invalid_text)
    {
      const File k_input = makeInput(k_invalid_text);
      const File k_output = makeScratch();
      const File k_error = makeScratch();
      np::StandardStreams streams(k_input.get(), k_output.get(), k_error.get());
      auto record = streams.nextRecord();
      REQUIRE(record.has_value());
      REQUIRE(record->has_value());
      CHECK(record->value().line == 1);
      CHECK(!record->value().valid_utf8);
    }
  }
}

TEST_CASE("Unreadable input is a fatal read error, not EOF")
{
  std::FILE* raw_input = nullptr;
  REQUIRE(fopen_s(&raw_input, "NUL", "wb") == 0);
  const File k_input(raw_input);
  const File k_output = makeScratch();
  const File k_error = makeScratch();
  np::StandardStreams streams(k_input.get(), k_output.get(), k_error.get());
  auto result = streams.nextRecord();
  REQUIRE(!result.has_value());
  CHECK(!result.error().reason.empty());
}

TEST_CASE("A read failure discards bytes already collected for a record")
{
  int descriptors[2] = {-1, -1};
  REQUIRE(_pipe(descriptors, 4096, _O_BINARY) == 0);
  std::FILE* raw_input = _fdopen(descriptors[0], "rb");
  REQUIRE(raw_input != nullptr);
  const File k_input(raw_input);
  constexpr std::string_view k_bytes = "first\npartial";
  REQUIRE(_write(descriptors[1], k_bytes.data(),
                 static_cast<unsigned int>(k_bytes.size())) ==
          static_cast<int>(k_bytes.size()));
  const File k_output = makeScratch();
  const File k_error = makeScratch();
  np::StandardStreams streams(k_input.get(), k_output.get(), k_error.get());
  auto first = streams.nextRecord();
  REQUIRE(first.has_value());
  REQUIRE(first->has_value());
  CHECK(first->value().text == "first");
  std::atomic<bool> failed = false;
  std::atomic<bool> finished = false;
  std::jthread reader([&] {
    const auto k_result = streams.nextRecord();
    const auto k_again = streams.nextRecord();
    failed = !k_result && !k_again &&
             k_result.error().reason == k_again.error().reason;
    finished = true;
  });
  const auto k_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  bool cancelled = false;
  while (!cancelled && !finished.load() &&
         std::chrono::steady_clock::now() < k_deadline)
  {
    cancelled = CancelSynchronousIo(reader.native_handle()) != 0;
    if (!cancelled)
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  const int k_close_result = _close(descriptors[1]);
  reader.join();
  CHECK(cancelled);
  CHECK(k_close_result == 0);
  CHECK(failed.load());
}

TEST_CASE("Writes retain UTF-8 and LF bytes and report delivery failure")
{
  const File k_input = makeInput("");
  const File k_output = makeScratch();
  const File k_error = makeScratch();
  np::StandardStreams streams(k_input.get(), k_output.get(), k_error.get());
  REQUIRE(streams.writeStdout("漢字\n").has_value());
  REQUIRE(streams.writeStderr("проект\n").has_value());
  REQUIRE(std::fseek(k_output.get(), 0, SEEK_SET) == 0);
  REQUIRE(std::fseek(k_error.get(), 0, SEEK_SET) == 0);
  CHECK(readAll(k_output.get()) == "漢字\n");
  CHECK(readAll(k_error.get()) == "проект\n");

  std::FILE* raw_read_only = nullptr;
  REQUIRE(fopen_s(&raw_read_only, "NUL", "rb") == 0);
  const File k_read_only(raw_read_only);
  REQUIRE(k_read_only != nullptr);
  np::StandardStreams failed(k_input.get(), k_read_only.get(), k_error.get());
  auto delivery = failed.writeStdout("unwritable\n");
  REQUIRE(!delivery.has_value());
  CHECK(!delivery.error().reason.empty());
}

TEST_CASE("A closed downstream pipe fails the checked flush")
{
  int descriptors[2] = {-1, -1};
  REQUIRE(_pipe(descriptors, 4096, _O_BINARY) == 0);
  std::FILE* raw_output = _fdopen(descriptors[1], "wb");
  REQUIRE(raw_output != nullptr);
  const File k_output(raw_output);
  REQUIRE(std::setvbuf(k_output.get(), nullptr, _IOFBF, 4096) == 0);
  REQUIRE(_close(descriptors[0]) == 0);
  const File k_input = makeInput("");
  const File k_error = makeScratch();
  np::StandardStreams streams(k_input.get(), k_output.get(), k_error.get());
  auto delivery = streams.writeStdout("partial\n");
  REQUIRE(!delivery.has_value());
  CHECK(delivery.error().reason == "standard stream flush failed");
}

TEST_CASE("Unbuffered writes preserve UTF-8 code point boundaries")
{
  constexpr std::array<std::string_view, 3> k_texts = {
      "é漢😀\n",
      "aé漢😀\n",
      "C:/Users/漢字/xy\n",
  };
  int case_number = 0;
  for (const auto k_text : k_texts)
  {
    DYNAMIC_SECTION("record length " << k_text.size())
    {
      const std::wstring k_name = L"\\\\.\\pipe\\np-streams-" +
                                  std::to_wstring(GetCurrentProcessId()) +
                                  L"-" + std::to_wstring(case_number++);
      HANDLE k_raw_server = CreateNamedPipeW(
          k_name.c_str(), PIPE_ACCESS_INBOUND,
          PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_NOWAIT, 1, 0, 4096,
          0, nullptr);
      REQUIRE(k_raw_server != INVALID_HANDLE_VALUE);
      const WinHandle k_server(k_raw_server);
      HANDLE k_raw_client = CreateFileW(k_name.c_str(), GENERIC_WRITE, 0,
                                        nullptr, OPEN_EXISTING, 0, nullptr);
      REQUIRE(k_raw_client != INVALID_HANDLE_VALUE);
      WinHandle client(k_raw_client);
      const int k_descriptor = _open_osfhandle(
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<intptr_t>(client.get()), _O_BINARY | _O_WRONLY);
      REQUIRE(k_descriptor >= 0);
      // NOLINTNEXTLINE(bugprone-unused-return-value)
      (void)client.release();  // The CRT descriptor now owns the OS handle.
      const File k_output(_fdopen(k_descriptor, "wb"));
      if (k_output == nullptr)
        (void)_close(k_descriptor);
      REQUIRE(k_output != nullptr);
      REQUIRE(std::setvbuf(k_output.get(), nullptr, _IONBF, 0) == 0);
      const File k_input = makeInput("");
      const File k_error = makeScratch();
      np::StandardStreams streams(k_input.get(), k_output.get(), k_error.get());
      REQUIRE(streams.writeStdout(k_text).has_value());

      std::string assembled;
      while (assembled.size() < k_text.size())
      {
        std::array<char, 64> message {};
        DWORD count = 0;
        REQUIRE(ReadFile(k_server.get(), message.data(),
                         static_cast<DWORD>(message.size()), &count,
                         nullptr) != 0);
        REQUIRE(count > 0);
        assembled.append(message.data(), count);
        REQUIRE(assembled.size() <= k_text.size());
        if (assembled.size() < k_text.size())
        {
          CHECK((static_cast<unsigned char>(k_text.at(assembled.size())) &
                 0xC0U) != 0x80U);
        }
      }
      CHECK(assembled == k_text);
    }
  }
}

// NOLINTEND(bugprone-unchecked-optional-access,readability-function-cognitive-complexity)
