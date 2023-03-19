#ifndef PZPIPE_UTILS_H
#define PZPIPE_UTILS_H
#include <cassert>
#include <optional>
#include <string>

extern bool DEBUG_MODE; //debug mode (default: off)

int auto_detected_thread_count();

// This is to be able to print to the console during stdout mode, as prints would get mixed with actual data otherwise, and not be displayed anyways
void print_to_console(std::string format);

template< typename... Args >
void print_to_console(const char* format, Args... args) {
  int length = std::snprintf(nullptr, 0, format, args...);
  assert(length >= 0);

  char* buf = new char[length + 1];
  std::snprintf(buf, length + 1, format, args...);

  std::string str(buf);
  delete[] buf;
  print_to_console(str);
}

char get_char_with_echo();

// batch error levels
constexpr auto ERR_DONT_USE_SPACE = 10;
constexpr auto ERR_MORE_THAN_ONE_OUTPUT_FILE = 11;
constexpr auto ERR_MORE_THAN_ONE_INPUT_FILE = 12;
constexpr auto ERR_CTRL_C = 13;
constexpr auto ERR_ONLY_SET_LZMA_THREAD_ONCE = 17;

void error(int error_nr, std::string tmp_filename = "");

long long get_time_ms();

void print_work_sign(bool with_backspace);

extern int old_lzma_progress_text_length;
void show_progress(float percent, bool use_backspaces, bool check_time, std::optional<int> lzma_mib_total = std::nullopt, std::optional<int> lzma_mib_written = std::nullopt);
#endif // PZPIPE_UTILS_H
