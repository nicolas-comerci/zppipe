#ifndef PZPIPE_IO_H
#define PZPIPE_IO_H
#include "pzpipe_utils.h"

#include "contrib/zpaq/libzpaq.h"

#include <memory>
#include <fstream>
#include <functional>
#include <queue>
#include <thread>
#include <utility>

class CompressedOStreamBuffer;
constexpr auto CHUNK = 262144 * 4 * 10; // 10 MB buffersize

class ZpaqIStreamBuffer : public std::streambuf
{
  class ZpaqIStreamBufReader : public libzpaq::Reader
  {
  public:
    std::vector<char> otf_in;
    std::istream* streambuf_wrapped_istream;
    long long curr_read_slot = 0;
    long long eof_slot = -1;

    explicit ZpaqIStreamBufReader(std::istream* wrapped_istream) : streambuf_wrapped_istream(wrapped_istream) {}

    explicit ZpaqIStreamBufReader(std::vector<char>&& otf_in)
      : otf_in(std::move(otf_in)), streambuf_wrapped_istream(nullptr), eof_slot(this->otf_in.size()) {}

    // Using this indirect way of getting pointers to the vector using slot to ensure we don't have problems after memory reallocations
    char* curr_read_ptr()
    {
      return otf_in.empty() ? nullptr : otf_in.data() + curr_read_slot;
    }

    char* eof_ptr()
    {
      return eof_slot < 0 ? nullptr : otf_in.data() + eof_slot;
    }

    int get() override {
      if (curr_read_ptr() != nullptr && curr_read_ptr() == eof_ptr()) return EOF;
      if (curr_read_ptr() == nullptr || curr_read_ptr() == otf_in.data() + otf_in.size()) {
        otf_in.reserve(otf_in.capacity() + CHUNK);
        if (curr_read_ptr() == nullptr) curr_read_slot = 0;

        auto tmp_buf = std::make_unique<char[]>(CHUNK);
        streambuf_wrapped_istream->read(tmp_buf.get(), CHUNK);
        const auto read_count = streambuf_wrapped_istream->gcount();

        if (read_count < CHUNK) eof_slot = curr_read_slot + read_count;
        if (read_count == 0) return EOF;

        for (char* curr = tmp_buf.get(); curr < tmp_buf.get() + read_count; curr++)
        {
          otf_in.push_back(*curr);
        }
      }
      const auto chr = static_cast<unsigned char>(*curr_read_ptr());
      curr_read_slot++;
      return chr;
    }

    std::vector<char> buffer_discard_old_data() {
      const char* data_end_ptr = eof_ptr() != nullptr ? eof_ptr() : otf_in.data() + otf_in.size();
      // ZPAQ uses a 64Kb buffer, so the actual start of the next block might be as far as 64Kb before the current reading position
      const char* data_start_ptr = curr_read_ptr() - (1 << 16) < otf_in.data() ? otf_in.data() : curr_read_ptr() - (1 << 16);
      const long long remaining_data_size = data_end_ptr - data_start_ptr;
      // copy the remaining data to a new vector
      std::vector<char> new_otf_in{};
      if (remaining_data_size > 0) new_otf_in.reserve(CHUNK);
      for (const char* curr = data_start_ptr; curr < data_end_ptr; curr++)
      {
        new_otf_in.push_back(*curr);
      }
      new_otf_in.swap(otf_in); // replace the old vector and free/transfer its memory (as its going out of scope/getting returned next)
      new_otf_in.resize(curr_read_slot);
      new_otf_in.shrink_to_fit();

      if (eof_ptr() != nullptr)  // adjust eof if necessary
      {
        eof_slot = eof_ptr() - data_start_ptr;  // remaining data before eof
      }
      curr_read_slot = 0;
      return new_otf_in;
    }
  };

  class ZpaqIStreamBufWriter : public libzpaq::Writer
  {
  public:
    std::unique_ptr<char[]>* dec_buf;
    char* curr_write = nullptr;

    explicit ZpaqIStreamBufWriter(std::unique_ptr<char[]>* otf_dec) : dec_buf(otf_dec) {}

    void put(int c) override {
      if (curr_write == nullptr) reset_write_ptr();
      *curr_write = c;
      curr_write++;
    }

    void reset_write_ptr() { curr_write = dec_buf->get(); }
  };

  class ZpaqIStreamBlockManager
  {
  public:
    ZpaqIStreamBufReader reader;
    std::unique_ptr<char[]> dec_buf;
    ZpaqIStreamBufWriter writer;
    std::thread decompression_thread;

    explicit ZpaqIStreamBlockManager(std::vector<char>&& otf_in)
      : reader(std::move(otf_in)), dec_buf(std::make_unique<char[]>(CHUNK * 10)), writer(&this->dec_buf) {}

    void decompress_on_thread()
    {
      decompression_thread = std::thread(&ZpaqIStreamBlockManager::decompress, this);
    }

    void decompress()
    {
      libzpaq::Decompresser decompresser;
      decompresser.setInput(&reader);
      decompresser.setOutput(&writer);
      decompresser.findBlock();
      decompresser.findFilename(); // This finds the segment
      decompresser.readComment();
      decompresser.decompress(-1);
      decompresser.readSegmentEnd();
    }
  };
public:
  std::istream* wrapped_istream;
  bool owns_wrapped_istream = false;
  std::unique_ptr<char[]> otf_dec;
  ZpaqIStreamBufReader reader;
  ZpaqIStreamBufWriter writer;
  std::queue<std::unique_ptr<ZpaqIStreamBlockManager>> block_managers;
  unsigned int max_thread_count;

  ZpaqIStreamBuffer(std::unique_ptr<std::istream>&& wrapped_istream, unsigned int max_thread_count)
    : reader(wrapped_istream.get()), writer(&this->otf_dec), max_thread_count(max_thread_count) {
    this->wrapped_istream = wrapped_istream.release();
    owns_wrapped_istream = true;
    init();
  }

  static std::unique_ptr<std::istream> from_istream(std::unique_ptr<std::istream>&& istream, unsigned int max_thread_count) {
    auto new_fin = std::unique_ptr<std::istream>(new std::ifstream());
    auto zpaq_streambuf = new ZpaqIStreamBuffer(std::move(istream), max_thread_count);
    new_fin->rdbuf(zpaq_streambuf);
    return new_fin;
  }

  void init() {
    otf_dec = std::make_unique<char[]>(CHUNK * 10);

    setg(otf_dec.get(), otf_dec.get(), otf_dec.get());
  }

  ~ZpaqIStreamBuffer() override {
    if (owns_wrapped_istream) delete wrapped_istream;
  }

  static bool findBlock(libzpaq::Decompresser& zpaq_decompresser) {
    bool found = true;
    double memory;                         // bytes required to decompress
    found &= zpaq_decompresser.findBlock(&memory);
    if (!found) return found;
    found &= zpaq_decompresser.findFilename(); // This finds the segment
    if (!found) return found;
    zpaq_decompresser.readComment();
    return found;
  }

  int underflow() override {
    if (gptr() < egptr())
      return *gptr();

    print_work_sign(true);

    // Make sure we have max_thread_count threads launched at any time (unless EOF already reached)
    while (block_managers.size() < max_thread_count)
    {
      libzpaq::Decompresser zpaq_decompresser;
      zpaq_decompresser.setInput(&reader);
      zpaq_decompresser.setOutput(&writer);
      const bool found = findBlock(zpaq_decompresser);
      if (!found) break; // This should only happen if we are at the original istream EOF so there are no more blocks to decompress
      zpaq_decompresser.readSegmentEnd();

      auto full_compressed_block = reader.buffer_discard_old_data();
      const auto& manager = block_managers.emplace(new ZpaqIStreamBlockManager(std::move(full_compressed_block)));
      manager->decompress_on_thread();
    }

    // As we need more data, we will need to wait for and get the data for the thread processing the next block
    const auto& front_block_manager = block_managers.front();
    front_block_manager->decompression_thread.join();
    int amt_read = front_block_manager->writer.curr_write - front_block_manager->writer.dec_buf->get();
    for (long long slot = 0; slot < amt_read; slot++)
    {
      *(otf_dec.get() + slot) = *(front_block_manager->writer.dec_buf->get() + slot);
    }
    block_managers.pop();
    
    setg(otf_dec.get(), otf_dec.get(), otf_dec.get() + amt_read);
    if (amt_read == 0) return EOF;
    writer.reset_write_ptr();
    return static_cast<unsigned char>(*gptr());
  }
};

std::unique_ptr<std::istream> wrap_istream_otf_compression(std::unique_ptr<std::istream>&& istream, unsigned int max_thread_count);

class CompressedOStreamBuffer : public std::streambuf
{
public:
  std::unique_ptr<std::ostream> wrapped_ostream;
  bool is_stream_eof = false;
  std::unique_ptr<char[]> otf_in;
  std::unique_ptr<char[]> otf_out;

  explicit CompressedOStreamBuffer(std::unique_ptr<std::ostream>&& wrapped_ostream): wrapped_ostream(std::move(wrapped_ostream))
  {
    otf_in = std::make_unique<char[]>(CHUNK);
    otf_out = std::make_unique<char[]>(CHUNK);
    setp(otf_in.get(), otf_in.get() + CHUNK);
  }

  virtual int sync(bool final_byte) = 0;
  int sync() override { return sync(false); }

  void set_stream_eof() {
    if (is_stream_eof) return;
    sync(true);
    is_stream_eof = true;
  }

  int overflow(int c) override {
    if (c == EOF) {
      set_stream_eof();
      return c;
    }

    if (pptr() == epptr()) {
      sync();
    }
    *pptr() = c;
    pbump(1);

    return c;
  }
};

class ZpaqOStreamBuffer : public CompressedOStreamBuffer
{
  class ZpaqOStreamBufReader : public libzpaq::Reader
  {
  public:
    std::unique_ptr<char[]> buffer;
    char* curr_read = nullptr;
    char* data_end;

    explicit ZpaqOStreamBufReader(std::unique_ptr<char[]>* otf_in) {
      buffer = std::make_unique<char[]>(CHUNK);
      std::copy_n(otf_in->get(), CHUNK, buffer.get());
      data_end = buffer.get() + CHUNK;
    }

    int read(char* buf, int n) override {
      if (curr_read == nullptr) reset_read_ptr();
      if (curr_read == data_end) return 0;
      const auto remaining_data_size = data_end - curr_read;
      auto read_size = remaining_data_size;
      if (n < remaining_data_size) {
        read_size = n;
      }
      memcpy(buf, curr_read, read_size);
      curr_read += read_size;
      return read_size;
    }

    int get() override {
      if (curr_read == nullptr) reset_read_ptr();
      if (curr_read == data_end) return EOF;
      const auto chr = static_cast<unsigned char>(*curr_read);
      curr_read++;
      return chr;
    }

    void reset_read_ptr() { curr_read = buffer.get(); }
  };

  class ZpaqOStreamBufWriter : public libzpaq::Writer
  {
  public:
    std::unique_ptr<char[]> buffer;
    char* current_buffer_pos = nullptr;

    ZpaqOStreamBufWriter()
    {
      buffer = std::make_unique<char[]>(2*CHUNK);
      current_buffer_pos = buffer.get();
    }

    void write(const char* buf, int n) override {
      std::copy_n(buf, n, current_buffer_pos);
      current_buffer_pos += n;
    }

    void put(int c) override {
      *current_buffer_pos = c;
      current_buffer_pos++;
    }

    [[nodiscard]] long long written_amt() const { return current_buffer_pos - buffer.get(); }
  };

  class ZpaqOstreamBlockManager
  {
  public:
    libzpaq::Compressor compressor;
    ZpaqOStreamBufReader reader;
    ZpaqOStreamBufWriter writer;
    std::thread compression_thread;
    bool compression_finished = false;

    explicit ZpaqOstreamBlockManager(std::unique_ptr<char[]>* otf_in) : reader(otf_in)
    {
      compressor.setInput(&reader);
      compressor.setOutput(&writer);
    }

    void compress_on_thread(const bool final_byte, long long size)
    {
      compression_thread = std::thread(&ZpaqOstreamBlockManager::compress, this, final_byte, size);
    }

    void compress(const bool final_byte, long long size)
    {
      if (final_byte) {
        reader.data_end = reader.buffer.get() + size;
      }

      compressor.writeTag();
      compressor.startBlock(2);
      compressor.startSegment();
      compressor.compress(CHUNK);
      compressor.endSegment();
      compressor.endBlock();
      reader.reset_read_ptr();
      compression_finished = true;
    }

    void write_to_ostream(std::ostream& ostream) const
    {
      ostream.write(writer.buffer.get(), writer.written_amt());
    }
  };
public:
  std::queue<std::unique_ptr<ZpaqOstreamBlockManager>> block_managers;
  unsigned int max_thread_count;

  ZpaqOStreamBuffer(std::unique_ptr<std::ostream>&& wrapped_ostream, unsigned int max_thread_count)
    : CompressedOStreamBuffer(std::move(wrapped_ostream)), max_thread_count(max_thread_count) { }

  static std::unique_ptr<std::ostream> from_ostream(std::unique_ptr<std::ostream>&& ostream, unsigned int max_thread_count);

  void write_blocks_finished_compressing(bool final_byte)
  {
    while (!block_managers.empty())
    {
      const auto& manager = block_managers.front();
      if (!manager->compression_finished && !final_byte && block_managers.size() != max_thread_count)
      {
        // If we are at the final byte of the stream we want to wait and dump everything to the ostream (as this function won't be called again),
        // and if we are already at the max_thread_count we want to wait until at least a thread slot is emptied, so we never go over that thread count.
        // In any other case, we break out of here, postponing the dumping to ostream, and allow the threads to continue running.
        break;
      }
      manager->compression_thread.join();
      manager->write_to_ostream(*this->wrapped_ostream);
      block_managers.pop();
    }
  }

  int sync(bool final_byte) override {
    const auto& manager = block_managers.emplace(new ZpaqOstreamBlockManager(&this->otf_in));
    manager->compress_on_thread(final_byte, pptr() - pbase());
    write_blocks_finished_compressing(final_byte); // dump to the ostream any finished blocks

    setp(otf_in.get(), otf_in.get() + CHUNK);
    return 0;
  }
};

std::unique_ptr<std::ostream> wrap_ostream_otf_compression(
  std::unique_ptr<std::ostream>&& ostream,
  unsigned int compression_otf_thread_count
);
#endif // PZPIPE_IO_H
