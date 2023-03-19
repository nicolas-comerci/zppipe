#include "pzpipe_io.h"

template <typename T>
class PZPipe_OStream : public T
{
  static_assert(std::is_base_of_v<std::ostream, T>, "OStreamWrapper must get an std::ostream derivative as template parameter");
public:
  std::unique_ptr<CompressedOStreamBuffer> otf_compression_streambuf;
  void rdbuf(std::streambuf * streambuffer)
  {
    std::ostream& stream_ref = *this;
    stream_ref.rdbuf(streambuffer);
  }

  ~PZPipe_OStream() {
    if (otf_compression_streambuf != nullptr) otf_compression_streambuf->set_stream_eof();
  }
};

std::unique_ptr<std::istream> wrap_istream_otf_compression(std::unique_ptr<std::istream>&& istream, unsigned int max_thread_count) {
  return ZpaqIStreamBuffer::from_istream(std::move(istream), max_thread_count);
}

void libzpaq::error(const char* msg) {  // print message and exit
  fprintf(stderr, "Oops: %s\n", msg);
  exit(1);
}

std::unique_ptr<std::ostream> ZpaqOStreamBuffer::from_ostream(std::unique_ptr<std::ostream>&& ostream, unsigned int max_thread_count) {
  auto new_fout = new PZPipe_OStream<std::ofstream>();
  auto zpaq_streambuf = new ZpaqOStreamBuffer(std::move(ostream), max_thread_count);
  new_fout->otf_compression_streambuf = std::unique_ptr<ZpaqOStreamBuffer>(zpaq_streambuf);
  new_fout->rdbuf(zpaq_streambuf);
  return std::unique_ptr<std::ostream>(new_fout);
}

std::unique_ptr<std::ostream> wrap_ostream_otf_compression(
  std::unique_ptr<std::ostream>&& ostream,
  unsigned int compression_otf_thread_count
) {
  return ZpaqOStreamBuffer::from_ostream(std::move(ostream), compression_otf_thread_count);
}
