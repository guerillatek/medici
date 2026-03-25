#pragma once
#include <streambuf>
#include <zlib.h>

template <size_t buffer_size = 16384> class gzFileBuf : public std::streambuf {
  gzFile file;
  char buffer[buffer_size];

public:
  gzFileBuf(const char *path, const char *mode) {
    file = gzopen(path, mode);
    setg(buffer, buffer, buffer);
    setp(buffer, buffer + buffer_size);
  }

  ~gzFileBuf() {
    sync();
    if (file)
      gzclose(file);
  }

  bool isOpen() const { return file != nullptr; }

protected:
  int underflow() override {
    if (!file)
      return EOF;

    int bytes = gzread(file, buffer, buffer_size);
    if (bytes <= 0)
      return EOF;

    setg(buffer, buffer, buffer + bytes);
    return (unsigned char)*gptr();
  }

  int overflow(int c) override {
    if (!file)
      return EOF;

    int n = pptr() - pbase();
    if (gzwrite(file, pbase(), n) != n)
      return EOF;

    setp(buffer, buffer + buffer_size);

    if (c != EOF) {
      *pptr() = c;
      pbump(1);
    }

    return c;
  }

  int sync() override { return overflow(EOF) == EOF ? -1 : 0; }
};