// Copyright 2020 The Pigweed Authors
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License. You may obtain a copy of
// the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

// This program generates Python test data for decoder_test.py.
//
// To generate the test data, build the target
// pw_tokenizer_generate_decoding_test_data. Execute the binary and move the
// generated files to this directory.

#include <array>
#include <cctype>
#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <random>

#include "pw_span/span.h"
#include "pw_tokenizer/internal/decode.h"
#include "pw_tokenizer/tokenize.h"
#include "pw_varint/varint.h"

namespace {

// Defines how to format test cases for the target language.
struct SourceFileFormat {
  const char* extension;
  const char* comment;
  const char* header;
  const char* footer;
  const char* test_case_prefix;
  const char* binary_string_prefix;
  const char* binary_string_suffix;
};

// clang-format off
constexpr const char* kCopyrightLines[] = {
"Copyright 2020 The Pigweed Authors",
"",
"Licensed under the Apache License, Version 2.0 (the \"License\"); you may not",
"use this file except in compliance with the License. You may obtain a copy of",
"the License at",
"",
"    https://www.apache.org/licenses/LICENSE-2.0",
"",
"Unless required by applicable law or agreed to in writing, software",
"distributed under the License is distributed on an \"AS IS\" BASIS, WITHOUT",
"WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the",
"License for the specific language governing permissions and limitations under",
"the License.",
};
// clang-format on

// The header includes a %s for the name and a %s for the test case type.
constexpr const char kCcHeader[] = R"(#pragma once

#include <string_view>
#include <tuple>

namespace pw::test::%s {

using namespace std::literals::string_view_literals;

// clang-format off
using TestCase = %s;

inline constexpr TestCase kTestData[] = {
)";

constexpr const char kCcFooter[] = R"(
};

}  // namespace pw::test::%s
)";

constexpr const char kPythonHeader[] = R"("""Generated test data."""

# pylint: disable=line-too-long
# C++ test case type for %s:
#     %s


def TestCase(*args):  # pylint: disable=invalid-name
    return tuple(args)



TEST_DATA = (
)";

constexpr SourceFileFormat kCcFormat{
    ".h", "//", kCcHeader, kCcFooter, "TestCase", "\"", "\"sv"};

constexpr SourceFileFormat kPythonFormat{
    ".py", "#", kPythonHeader, "\n)\n", "", "b'", "'"};

class TestDataFile {
 public:
  TestDataFile(const char* name,
               const SourceFileFormat& format,
               const char* test_case_format)
      : format_(format),
        name_(name),
        test_case_format_(test_case_format),
        path_(std::string(name) + "_test_data" + format_.extension),
        file_(std::fopen(path_.c_str(), "w")) {}

  ~TestDataFile() { std::fclose(file_); }

  const SourceFileFormat& fmt() const { return format_; }
  const std::string& path() const { return path_; }

  // Writes a file with test cases uses the provided function.
  void WriteTestCases(void (*function)(TestDataFile*)) {
    static constexpr const char* kFileBase =
        &__FILE__[std::string_view(__FILE__).find_last_of('/') + 1];

    for (const char* line : kCopyrightLines) {
      printf("%s", fmt().comment);
      if (line[0] == '\0') {
        printf("\n");
      } else {
        printf(" %s\n", line);
      }
    }

    printf("\n%s AUTOGENERATED - DO NOT EDIT\n", fmt().comment);
    printf("%s This file contains test data generated by %s.\n",
           fmt().comment,
           kFileBase);

    printf(fmt().header, name_, test_case_format_);
    function(this);
    printf(fmt().footer, name_);
  }

  // Starts a section of test cases in the file.
  void Section(const char* comment) {
    printf("\n%s %s\n", fmt().comment, comment);
  }

  int printf(const char* format, ...) PW_PRINTF_FORMAT(2, 3) {
    va_list args;
    va_start(args, format);
    const int result = std::vfprintf(file_, format, args);
    va_end(args);
    return result;
  }

 private:
  SourceFileFormat format_;
  const char* name_;
  const char* test_case_format_;
  std::string path_;
  FILE* file_;
};

// Writes a decoding test case to the file.
void TestCase(TestDataFile* file,
              pw::span<const uint8_t> buffer,
              const char* format,
              const char* formatted) {
  file->printf(R"(TestCase("%s", "%s", %s)",
               format,
               formatted,
               file->fmt().binary_string_prefix);

  for (uint8_t byte : buffer) {
    file->printf("\\x%02x", byte);
  }

  file->printf("%s),\n", file->fmt().binary_string_suffix);
}

template <size_t kSize>
void TestCase(TestDataFile* file,
              const char* format,
              const char (&buffer)[kSize],
              const char* formatted) {
  TestCase(file,
           pw::span(reinterpret_cast<const uint8_t*>(buffer), kSize - 1),
           format,
           formatted);
}

// __VA_ARGS__ is expanded twice, so ONLY variables / constants should be used.
#define MAKE_TEST_CASE(format, ...)                                           \
  do {                                                                        \
    std::array<uint8_t, 128> buffer;                                          \
    size_t size = buffer.size();                                              \
    PW_TOKENIZE_TO_BUFFER(buffer.data(), &size, format, ##__VA_ARGS__);       \
                                                                              \
    std::array<char, 128> formatted = {};                                     \
    std::snprintf(formatted.data(), formatted.size(), format, ##__VA_ARGS__); \
    TestCase(file,                                                            \
             pw::span(buffer).first(size).subspan(4), /* skip the token */    \
             format,                                                          \
             formatted.data());                                               \
  } while (0)

// Formats the contents like an error.
#define ERROR_STR PW_TOKENIZER_ARG_DECODING_ERROR

// Generates data to test tokenized string decoding.
void GenerateEncodedStrings(TestDataFile* file) {
  std::mt19937 random(6006411);
  std::uniform_int_distribution<int64_t> big;
  std::uniform_int_distribution<int32_t> medium;
  std::uniform_int_distribution<int32_t> small(' ', '~');
  std::uniform_real_distribution<float> real;

  file->Section("Simple strings");
  TestCase(file, "%s", "\3SFO", "SFO");
  TestCase(file, "%s", "\4KSJC", "KSJC");
  TestCase(file, "%s", "\0", "");

  TestCase(file, "%5s%s", "\2no\3fun", "   nofun");
  TestCase(file, "%5s%s", "\6abcdef\0", "abcdef");
  TestCase(file, "%5s%s", "\0\6abcdef", "     abcdef");

  TestCase(file,
           "%s %-6s%s%s%s",
           "\5Intel\580586\7toaster\1 \4oven",
           "Intel 80586 toaster oven");
  TestCase(file,
           "%s %-6s%s%s%s",
           "\5Apple\x09"
           "automatic\7 pencil\1 \x09sharpener",
           "Apple automatic pencil sharpener");

  file->Section("Zero-length strings");
  TestCase(file, "%s-%s", "\x02so\x00", "so-");
  TestCase(file, "%s-%s", "\x00\04cool", "-cool");
  TestCase(file, "%s%s%3s%s", "\0\0\0\0", "   ");
  TestCase(file, "(%5s)(%2s)(%7s)", "\x80\0\x80", "([...])(  )(  [...])");

  file->Section("Invalid strings");
  TestCase(file, "%s", "\x03hi", ERROR_STR("%s ERROR (hi)"));
  TestCase(file, "%30s", "\x03hi", ERROR_STR("%30s ERROR (hi)"));
  TestCase(file, "%30s", "\x83hi", ERROR_STR("%30s ERROR (hi)"));
  TestCase(file, "%s", "\x85yo!", ERROR_STR("%s ERROR (yo!)"));
  TestCase(file, "%s", "\x01", ERROR_STR("%s ERROR"));
  TestCase(file, "%30s", "\x81", ERROR_STR("%30s ERROR"));

  file->Section("Continue after truncated string");
  TestCase(file, "%s %d %s", "\x82go\4\5lunch", "go[...] 2 lunch");
  TestCase(file, "%6s%s%s", "\x80\x85hello\x05there", " [...]hello[...]there");

  file->Section("Floating point");
  TestCase(file, "%1.1f", "\0\0\0\0", "0.0");
  TestCase(file, "%0.5f", "\xdb\x0f\x49\x40", "3.14159");

  file->Section("Character");  // ZigZag doubles the value of positive integers.
  TestCase(file, "%c", "\x40", " ");          // 0x20
  TestCase(file, "%c", "\x48", "$");          // 0x24
  TestCase(file, "%c", "\x48", "$");          // 0x24
  TestCase(file, "100%c!", "\x4A", "100%!");  // 0x25

  file->Section("Atypical argument types");
  MAKE_TEST_CASE("%ju", static_cast<uintmax_t>(99));
  MAKE_TEST_CASE("%jd", static_cast<intmax_t>(99));
  MAKE_TEST_CASE("%zu", sizeof(uint64_t));
  MAKE_TEST_CASE("%zd", static_cast<ptrdiff_t>(123));
  MAKE_TEST_CASE("%td", static_cast<ptrdiff_t>(99));

  file->Section("Percent character");
  TestCase(file, "%%", "", "%");
  TestCase(file, "%%%%%%%%", "abc", "%%%%");
  TestCase(file, "whoa%%%%wow%%%%!%%", "", "whoa%%wow%%!%");
  TestCase(file, "This is %d%% effective", "\x02", "This is 1% effective");
  TestCase(
      file, "%% is 100%sa%%sign%%%s", "\x01%\x03OK?", "% is 100%a%sign%OK?");

  file->Section("Percent character prints after errors");
  TestCase(file, "%s%%", "\x83-10\0", "-10[...]%");
  TestCase(
      file, "%d%% is a good %%", "", ERROR_STR("%d MISSING") "% is a good %");

  file->Section("Various format strings");
  MAKE_TEST_CASE("!");
  MAKE_TEST_CASE("%s", "%s");
  MAKE_TEST_CASE("%s", "hello");
  MAKE_TEST_CASE("%s%s", "Hello", "old");
  MAKE_TEST_CASE("%s to the%c%s", "hello", ' ', "whirled");
  MAKE_TEST_CASE("hello %s %d %d %d", "rolled", 1, 2, 3);

  TestCase(file, "", "", "");
  TestCase(file, "This has no specifiers", "", "This has no specifiers");
  TestCase(file, "%s_or_%3s", "\x05hello\x02hi", "hello_or_ hi");
  TestCase(file, "%s_or_%3d", "\x05hello\x7f", "hello_or_-64");
  TestCase(file,
           "%s or hi%c pi=%1.2e",
           "\x05hello\x42\xdb\x0f\x49\x40",
           "hello or hi! pi=3.14e+00");
  TestCase(file,
           "Why, %s there. My favorite number is %.2f%c",
           "\x05hello\xdb\x0f\x49\x40\x42",
           "Why, hello there. My favorite number is 3.14!");

  file->Section("Various errors");
  TestCase(file, "%d", "", ERROR_STR("%d MISSING"));

  TestCase(file,
           "ABC%d123%dabc%dABC",
           "",
           "ABC" ERROR_STR("%d MISSING") "123" ERROR_STR(
               "%d SKIPPED") "abc" ERROR_STR("%d SKIPPED") "ABC");

  TestCase(file,
           "%sXY%+ldxy%a",
           "\x83Yo!\x80",
           "Yo![...]XY" ERROR_STR("%+ld ERROR") "xy" ERROR_STR("%a SKIPPED"));

  TestCase(file, "%d", "", ERROR_STR("%d MISSING"));

  TestCase(file,
           "%sXY%+ldxy%a",
           "\x83Yo!\x80",
           "Yo![...]XY" ERROR_STR("%+ld ERROR") "xy" ERROR_STR("%a SKIPPED"));

  TestCase(file,
           "%s%lld%9u",
           "\x81$\x80\x80",
           "$[...]" ERROR_STR("%lld ERROR") ERROR_STR("%9u SKIPPED"));

  file->Section("Alternate form (#)");
  MAKE_TEST_CASE("Hex: %#x", 0xbeef);
  MAKE_TEST_CASE("Hex: %#08X", 0xfeed);

  file->Section("Random integers");
  for (int i = 0; i < 100; ++i) {
    float f = real(random);
    MAKE_TEST_CASE(
        "This is a number: %+08.3e%1.0E%02d%g%G%f%-3f", f, f, i, f, f, f, f);
  }

  for (int i = 0; i < 100; ++i) {
    unsigned long long n1 = big(random);
    int n2 = medium(random);
    char ch = static_cast<char>(small(random));
    if (ch == '"' || ch == '\\') {
      ch = '\t';
    }

    MAKE_TEST_CASE("%s: %llu %d %c", std::to_string(i).c_str(), n1, n2, ch);
  }

  for (int i = 0; i < 100; ++i) {
    const long long n1 = big(random);
    const unsigned n2 = medium(random);
    const char ch = static_cast<char>(small(random));

    MAKE_TEST_CASE(
        "%s: %lld 0x%16u%08X %d", std::to_string(i).c_str(), n1, n2, n2, ch);
  }
}

template <typename T>
void OutputVarintTest(TestDataFile* file, T i) {
  if constexpr (sizeof(T) <= sizeof(int)) {
    file->printf(R"(TestCase("%%d", "%d", "%%u", "%u", %s)",
                 static_cast<int>(i),
                 static_cast<unsigned>(i),
                 file->fmt().binary_string_prefix);
  } else {
    file->printf(R"(TestCase("%%lld", "%lld", "%%llu", "%llu", %s)",
                 static_cast<long long>(i),
                 static_cast<unsigned long long>(i),
                 file->fmt().binary_string_prefix);
  }

  std::array<uint8_t, 10> buffer;
  // All integers are encoded as signed for tokenization.
  size_t size = pw::varint::Encode(i, pw::as_writable_bytes(pw::span(buffer)));

  for (size_t i = 0; i < size; ++i) {
    file->printf("\\x%02x", buffer[i]);
  }

  file->printf("%s),\n", file->fmt().binary_string_suffix);
}

// Generates data to test variable-length integer decoding.
void GenerateVarints(TestDataFile* file) {
  std::mt19937 random(6006411);
  std::uniform_int_distribution<int64_t> signed64;
  std::uniform_int_distribution<int32_t> signed32;
  std::uniform_int_distribution<int16_t> signed16;

  file->Section("Important numbers");
  OutputVarintTest(file, 0);
  OutputVarintTest(file, std::numeric_limits<int16_t>::min());
  OutputVarintTest(file, std::numeric_limits<int16_t>::min() + 1);
  OutputVarintTest(file, std::numeric_limits<int16_t>::max() - 1);
  OutputVarintTest(file, std::numeric_limits<int16_t>::max());
  OutputVarintTest(file, std::numeric_limits<int32_t>::min());
  OutputVarintTest(file, std::numeric_limits<int32_t>::min() + 1);
  OutputVarintTest(file, std::numeric_limits<int32_t>::max() - 1);
  OutputVarintTest(file, std::numeric_limits<int32_t>::max());
  OutputVarintTest(file, std::numeric_limits<int64_t>::min());
  OutputVarintTest(file, std::numeric_limits<int64_t>::min() + 1);
  OutputVarintTest(file, std::numeric_limits<int64_t>::max() - 1);
  OutputVarintTest(file, std::numeric_limits<int64_t>::max());

  file->Section("Random 64-bit ints");
  for (int i = 0; i < 500; ++i) {
    OutputVarintTest(file, signed64(random));
  }
  file->Section("Random 32-bit ints");
  for (int i = 0; i < 100; ++i) {
    OutputVarintTest(file, signed32(random));
  }
  file->Section("Random 16-bit ints");
  for (int i = 0; i < 100; ++i) {
    OutputVarintTest(file, signed16(random));
  }

  file->Section("All 8-bit numbers");
  {
    int i = std::numeric_limits<int8_t>::min();
    while (true) {
      OutputVarintTest(file, i);
      if (i == std::numeric_limits<int8_t>::max()) {
        break;
      }
      // Don't use an inline increment to avoid undefined behavior (overflow).
      i += 1;
    }
  }
}

template <typename Function>
void WriteFile(const char* name,
               const char* test_case_format,
               Function function) {
  for (const SourceFileFormat& file_format : {kCcFormat, kPythonFormat}) {
    TestDataFile file(name, file_format, test_case_format);
    file.WriteTestCases(function);

    std::printf("Wrote %s\n", file.path().c_str());
  }
}

}  // namespace

int main(int, char**) {
  WriteFile("tokenized_string_decoding",
            "std::tuple<const char*, std::string_view, std::string_view>",
            GenerateEncodedStrings);
  WriteFile("varint_decoding",
            "std::tuple<const char*, const char*, const char*, const char*, "
            "std::string_view>",
            GenerateVarints);
  return 0;
}
