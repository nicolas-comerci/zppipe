/* Copyright 2023 Nicolas Comerci

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License. */

// version information
#define V_MAJOR 0
#define V_MINOR 2
static constexpr char V_MINOR2 = 'a';
//#define V_STATE "ALPHA"
#define V_STATE "DEVELOPMENT"
//#define V_MSG "USE FOR TESTING ONLY"
#define V_MSG "USE AT YOUR OWN RISK!"
#ifdef __unix
  #define V_OS "Unix"
#else
  #define V_OS "Windows"
#endif
#ifdef BIT64
  #define V_BIT "64-bit"
#else
  #define V_BIT "32-bit"
#endif

#define NOMINMAX

#include <cstdio>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <array>
#include <csignal>
#include <random>
#include <fcntl.h>
#include <filesystem>
#include <set>
#ifdef _MSC_VER
#include <io.h>
#define ftruncate _chsize_s
#endif

#ifndef __unix
#include <conio.h>
#include <windows.h>
#include <io.h>
#define PATH_DELIM '\\'
#else
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#define PATH_DELIM '/'
#endif

#include <thread>
#include <cstdio>
#include <vector>
#include <string>
#include <fstream>
#include <memory>

// This I shamelessly lifted from https://web.archive.org/web/20090907131154/http://www.cs.toronto.edu:80/~ramona/cosmin/TA/prog/sysconf/
// (credit to this StackOverflow answer for pointing me to it https://stackoverflow.com/a/1613677)
// It allows us to portably (at least for Windows/Linux/Mac) set a std stream as binary
#define STDIN  0
#define STDOUT 1
#define STDERR 2
#ifndef __unix
# define SET_BINARY_MODE(handle) setmode(handle, O_BINARY)
#else
# define SET_BINARY_MODE(handle) ((void)0)
#endif

#include "pzpipe_io.h"

#define P_COMPRESS 1
#define P_DECOMPRESS 2

class PZPipe {
public:
    explicit PZPipe() {
        if (compression_otf_thread_count == 0) { compression_otf_thread_count = 2; }
    }

    unsigned int compression_otf_thread_count = std::thread::hardware_concurrency();

    long long fin_length;
    std::string input_file_name;
    std::string output_file_name;

    std::unique_ptr<std::istream> fin = std::unique_ptr<std::istream>(new std::ifstream());
    std::unique_ptr<std::ostream> fout = std::unique_ptr<std::ostream>(new std::ofstream());

    float global_min_percent = 0;
    float global_max_percent = 100;

    // Uncompressed data info
    long long uncompressed_bytes_total = 0;
    long long uncompressed_bytes_written = 0;
};

PZPipe g_pzpipe;

long long start_time;

int ostream_printf(std::ostream& out, std::string str) {
  for (char character : str) {
    out.put(character);
    if (out.bad()) return 0;
  }
  return str.length();
}

int parseInt(const char*& c, const char* context, int too_big_error_code = 0) {
    if (*c < '0' || *c > '9') {
        print_to_console("ERROR: Number needed to set %s\n", context);
        exit(1);
    }
    int val = *c++ - '0';
    while (*c >= '0' && *c <= '9') {
        if (val >= INT_MAX / 10 - 1) {
            if (too_big_error_code != 0) {
                error(too_big_error_code);
            }
            print_to_console("ERROR: Number too big for %s\n", context);
            exit(1);
        }
        val = val * 10 + *c++ - '0';
    }
    return val;
}

int parseIntUntilEnd(const char* c, const char* context, int too_big_error_code = 0) {
    for (int i = 0; c[i]; ++i) {
        if (c[i] < '0' || c[i] > '9') {
            print_to_console("ERROR: Only numbers allowed for %s\n", context);
            exit(1);
        }
    }
    const char* x = c;
    return parseInt(x, context, too_big_error_code);
}

void write_header() {
  // write the PCF file header, beware that this needs to be done before wrapping the output file with a CompressedOStreamBuffer
  char* input_file_name_without_path = new char[g_pzpipe.input_file_name.length() + 1];

  ostream_printf(*g_pzpipe.fout, "PCF");

  // version number
  g_pzpipe.fout->put(V_MAJOR);
  g_pzpipe.fout->put(V_MINOR);
  g_pzpipe.fout->put(V_MINOR2);

  // write input file name without path
  const char* last_backslash = strrchr(g_pzpipe.input_file_name.c_str(), PATH_DELIM);
  if (last_backslash != nullptr) {
    strcpy(input_file_name_without_path, last_backslash + 1);
  } else {
    strcpy(input_file_name_without_path, g_pzpipe.input_file_name.c_str());
  }

  ostream_printf(*g_pzpipe.fout, input_file_name_without_path);
  g_pzpipe.fout->put(0);

  delete[] input_file_name_without_path;
}

void read_header() {
  unsigned char in[3];
  g_pzpipe.fin->read(reinterpret_cast<char*>(in), 3);
  if ((in[0] == 'P') && (in[1] == 'C') && (in[2] == 'F')) {
  } else {
    print_to_console("Input file %s has no valid PCF header\n", g_pzpipe.input_file_name.c_str());
    exit(1);
  }

  g_pzpipe.fin->read(reinterpret_cast<char*>(in), 3);
  if ((in[0] == V_MAJOR) && (in[1] == V_MINOR) && (in[2] == V_MINOR2)) {
  } else {
    print_to_console("Input file %s was made with a different PZPipe version\n", g_pzpipe.input_file_name.c_str());
    print_to_console("PCF version info: %i.%i.%i\n", in[0], in[1], in[2]);
    exit(1);
  }

  std::string header_filename = "";
  char c;
  do {
    c = g_pzpipe.fin->get();
    if (c != 0) header_filename += c;
  } while (c != 0);

  if (g_pzpipe.output_file_name.empty()) {
    g_pzpipe.output_file_name = header_filename;
  }
}

long long fileSize64(const char* filename) {
    std::error_code ec;
    return std::filesystem::file_size(filename, ec);
}

bool file_exists(const char* filename) {
    std::fstream fin;
    fin.open(filename, std::ios::in);
    bool retval = fin.is_open();
    fin.close();

    return retval;
}

int init(int argc, char* argv[]) {
    int i;

    print_to_console("\n");
    print_to_console("PZPipe v%i.%i%c %s %s - %s version",V_MAJOR,V_MINOR,V_MINOR2,V_OS,V_BIT,V_STATE);
    print_to_console(" - %s\n",V_MSG);
    print_to_console("Apache 2.0 License - Copyright 2023 by Nicolas Comerci\n");
    print_to_console("  LibZPAQ by Matt Mahoney (https://mattmahoney.net/dc/zpaq.html)\n\n");

    bool valid_syntax = false;
    bool input_file_given = false;
    bool output_file_given = false;
    int operation = P_COMPRESS;
    bool parse_on = true;
    bool zpaq_thread_count_set = false;

    for (i = 1; (i < argc) && (parse_on); i++) {
        if (argv[i][0] == '-') { // switch
            if (input_file_given) {
                valid_syntax = false;
                parse_on = false;
                break;
            }
            switch (toupper(argv[i][1])) {
                case 0:
                {
                    valid_syntax = false;
                    parse_on = false;
                    break;
                }
                case 'T':
                {
                    if (zpaq_thread_count_set) {
                        error(ERR_ONLY_SET_ZPAQ_THREAD_ONCE);
                    }
                    g_pzpipe.compression_otf_thread_count = std::min<unsigned int>(
                            parseIntUntilEnd(argv[i] + 2, "ZPAQ thread count"),
                            std::thread::hardware_concurrency()
                    );
                    zpaq_thread_count_set = true;
                    break;
                }
                case 'V':
                {
                    DEBUG_MODE = true;
                    if (argv[i][2] != 0) { // Extra Parameters?
                        print_to_console("ERROR: Unknown switch \"%s\"\n", argv[i]);
                        exit(1);
                    }
                    break;
                }
                case 'D':
                {
                    operation = P_DECOMPRESS;
                    if (argv[i][2] != 0) { // Extra Parameters?
                        print_to_console("ERROR: Unknown switch \"%s\"\n", argv[i]);
                        exit(1);
                    }
                    break;
                }
                case 'O':
                {
                    if (output_file_given) {
                        error(ERR_MORE_THAN_ONE_OUTPUT_FILE);
                    }

                    if (strlen(argv[i]) == 2) {
                        error(ERR_DONT_USE_SPACE);
                    }

                    output_file_given = true;
                    g_pzpipe.output_file_name = argv[i] + 2;

                    break;
                }
                default:
                {
                    print_to_console("ERROR: Unknown switch \"%s\"\n", argv[i]);
                    exit(1);
                }
            }
        }
        else { // no switch
            if (input_file_given) {
                error(ERR_MORE_THAN_ONE_INPUT_FILE);
            }

            input_file_given = true;
            g_pzpipe.input_file_name = argv[i];

            if (g_pzpipe.input_file_name == "stdin") {
                // Read binary from stdin
                SET_BINARY_MODE(STDIN);
                g_pzpipe.fin->rdbuf(std::cin.rdbuf());
            } else {
                g_pzpipe.fin_length = fileSize64(argv[i]);

                auto fin = std::make_unique<std::ifstream>();
                fin->open(argv[i], std::ios_base::in | std::ios_base::binary);
                if (!fin->is_open()) {
                    print_to_console("ERROR: Input file \"%s\" doesn't exist\n", g_pzpipe.input_file_name.c_str());

                    exit(1);
                }
                g_pzpipe.fin = std::move(fin);
            }

            // output file given? If not, use input filename with .zpaq extension
            if ((!output_file_given) && (operation == P_COMPRESS)) {
                g_pzpipe.output_file_name = g_pzpipe.input_file_name + ".zpaq";
                output_file_given = true;
            }

            valid_syntax = true;
        }
    }

    if (!valid_syntax) {
        print_to_console("Usage: pzpipe [-switches] input_file\n\n");
        print_to_console("  d            Decompress ZPAQ stream\n");
        print_to_console("  o[filename]  Write output to [filename] <[input_file].zpaq or file in header>\n");
        print_to_console("  e            preserve original extension of input name for output name <off>\n");
        print_to_console("  t[count]     Set ZPAQ thread count <auto-detect: %i>\n", auto_detected_thread_count());
        print_to_console("  v            Verbose (debug) mode <off>\n");

        exit(1);
    }

    if (operation == P_DECOMPRESS) {
        read_header();
    }

    if (output_file_given && g_pzpipe.output_file_name == "stdout") {
        // Write binary to stdout
        SET_BINARY_MODE(STDOUT);
        g_pzpipe.fout->rdbuf(std::cout.rdbuf());
    }
    else {
        if (file_exists(g_pzpipe.output_file_name.c_str())) {
            print_to_console("Output file \"%s\" exists. Overwrite (y/n)? ", g_pzpipe.output_file_name.c_str());
            char ch = get_char_with_echo();
            if ((ch != 'Y') && (ch != 'y')) {
                print_to_console("\n");
                exit(0);
            }
            else {
#ifndef __unix
                print_to_console("\n\n");
#else
                print_to_console("\n");
#endif
            }
        }

        auto fout = std::make_unique<std::ofstream>();
        fout->open(g_pzpipe.output_file_name.c_str(), std::ios_base::out | std::ios_base::binary);
        if (!fout->is_open()) {
            print_to_console("ERROR: Can't create output file \"%s\"\n", g_pzpipe.output_file_name.c_str());
            exit(1);
        }
        g_pzpipe.fout = std::move(fout);
    }

    print_to_console("Input file: %s\n", g_pzpipe.input_file_name.c_str());
    print_to_console("Output file: %s\n\n", g_pzpipe.output_file_name.c_str());

    return operation;
}

// nice time output, input t in ms
// 2^32 ms maximum, so will display incorrect negative values after about 49 days
void printf_time(long long t) {
    print_to_console("Time: ");
    if (t < 1000) { // several milliseconds
        print_to_console("%li millisecond(s)\n", (long)t);
    } else if (t < 1000*60) { // several seconds
        print_to_console("%li second(s), %li millisecond(s)\n", (long)(t / 1000), (long)(t % 1000));
    } else if (t < 1000*60*60) { // several minutes
        print_to_console("%li minute(s), %li second(s)\n", (long)(t / (1000*60)), (long)((t / 1000) % 60));
    } else if (t < 1000*60*60*24) { // several hours
        print_to_console("%li hour(s), %li minute(s), %li second(s)\n", (long)(t / (1000*60*60)), (long)((t / (1000*60)) % 60), (long)((t / 1000) % 60));
    } else {
        print_to_console("%li day(s), %li hour(s), %li minute(s)\n", (long)(t / (1000*60*60*24)), (long)((t / (1000*60*60)) % 24), (long)((t / (1000*60)) % 60));
    }
}

void denit_compress() {
    g_pzpipe.fout = nullptr;
    long long fout_length = fileSize64(g_pzpipe.output_file_name.c_str());
    std::string result_print = "New size: " + std::to_string(fout_length) + " instead of " + std::to_string(g_pzpipe.fin_length) + "     \n";
    if (!DEBUG_MODE) {
        print_to_console("%s", std::string(14, '\b').c_str());
        print_to_console("100.00% - " + result_print);
    }
    else {
        print_to_console(result_print);
    }

    print_to_console("\nDone.\n");
    printf_time(get_time_ms() - start_time);
}

void denit_decompress() {
  if (!DEBUG_MODE) {
      print_to_console("%s", std::string(14,'\b').c_str());
      print_to_console("100.00%%\n");
  }
  print_to_console("\nDone.\n");
  printf_time(get_time_ms() - start_time);
}

static constexpr int COPY_BUF_SIZE = 512;

bool compress_file(float min_percent = 0, float max_percent = 100) {
  write_header();
  g_pzpipe.fout = wrap_ostream_otf_compression(
    std::move(g_pzpipe.fout),
    g_pzpipe.compression_otf_thread_count
  );

  g_pzpipe.global_min_percent = min_percent;
  g_pzpipe.global_max_percent = max_percent;

  long long uncompressed_length = -1;
  long long uncompressed_pos = 0;
  g_pzpipe.uncompressed_bytes_total = 0;
  g_pzpipe.uncompressed_bytes_written = 0;

  int bytes_read;
  unsigned char copybuf[COPY_BUF_SIZE];

  long long input_file_pos = 0;

  // uncompressed data
  g_pzpipe.fout->put(0);

  for (;;) {
    g_pzpipe.fin->read(reinterpret_cast<char*>(copybuf), COPY_BUF_SIZE);
    bytes_read = g_pzpipe.fin->gcount();
    if (bytes_read > -1) g_pzpipe.fout->write(reinterpret_cast<char*>(copybuf), bytes_read);
    if (bytes_read < COPY_BUF_SIZE) {
      break;
    }

    input_file_pos = g_pzpipe.fin->tellg();
    print_work_sign(true);
    if (!DEBUG_MODE) {
      float percent = (input_file_pos / (float)g_pzpipe.fin_length) * 100;
      show_progress(percent, true, true);
    }
  }

  denit_compress();

  return true;
}

void decompress_file() {
  g_pzpipe.fin = wrap_istream_otf_compression(std::move(g_pzpipe.fin), g_pzpipe.compression_otf_thread_count);

  if (!DEBUG_MODE) show_progress(0, false, false);

  long long fin_pos = g_pzpipe.fin->tellg();

  if (!DEBUG_MODE) {
    float percent = (fin_pos / (float)g_pzpipe.fin_length) * 100;
    show_progress(percent, true, true);
  }

  unsigned char header1 = g_pzpipe.fin->get();
  if (header1 == 0) { // uncompressed data
    int chr = 0;
    int next_char = g_pzpipe.fin->get();
    while (g_pzpipe.fin->good()) {
      chr = next_char;
      // This is a hack fix, for some reason we are adding a Zeroed byte at the end of the original stream while compressing.
      // The actual fix is to find out why that is happening and make it no longer happen, for now I just want this to work so if the next chr is EOF
      // I know we are currently at the last byte of actual data, which is that extra Zeroed byte, so I just drop it and end writing output at that point.
      next_char = g_pzpipe.fin->get();
      if (next_char == EOF) break;
      g_pzpipe.fout->put(chr);
      fin_pos += 1;
      if (fin_pos >= g_pzpipe.fin_length) fin_pos = g_pzpipe.fin_length - 1;
    }
  }

  denit_decompress();
}

void ctrl_c_handler(int sig) {
    print_to_console("\n\nCTRL-C detected\n");
    (void) signal(SIGINT, SIG_DFL);

    error(ERR_CTRL_C);
}

int main(int argc, char* argv[])
{
  // register CTRL-C handler
  (void) signal(SIGINT, ctrl_c_handler);

  switch (init(argc, argv)) {

    case P_COMPRESS:
      {
        start_time = get_time_ms();
        compress_file();
        break;
      }

    case P_DECOMPRESS:
      {
        start_time = get_time_ms();
        decompress_file();
        break;
      }
  }

  return 0;
}
