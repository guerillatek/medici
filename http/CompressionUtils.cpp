
#include "CompressionUtils.hpp"

#include <fstream>

namespace medici::http {

Expected compressRFC7692(std::string_view input, std::vector<uint8_t> &output,
                         int windowBits) {
  z_stream strm = {};

  // RFC 7692 requires raw DEFLATE format (negative windowBits)
  // window_bits_ can be negotiated between 8-15
  int ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                         -windowBits, // NEGATIVE for raw DEFLATE
                         8,           // Standard memory level
                         Z_DEFAULT_STRATEGY);

  if (ret != Z_OK) {
    throw std::runtime_error("Failed to initialize DEFLATE");
  }

  output.resize(input.size() + 32);

  strm.avail_in = input.size();
  strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.data()));
  strm.avail_out = output.size();
  strm.next_out = output.data();

  // RFC 7692: Use Z_SYNC_FLUSH for per-message compression
  ret = deflate(&strm, Z_SYNC_FLUSH);
  if (ret < 0) {
    deflateEnd(&strm);
    return std::unexpected(
        "RFC 7692: Use Z_SYNC_FLUSH for per-message compression failed");
  }

  // RFC 7692 step 3: Remove the 0x00 0x00 0xFF 0xFF trailer
  size_t compressed_size = strm.total_out;
  if (compressed_size >= 4 && output[compressed_size - 4] == 0x00 &&
      output[compressed_size - 3] == 0x00 &&
      output[compressed_size - 2] == 0xFF &&
      output[compressed_size - 1] == 0xFF) {
    compressed_size -= 4; // Remove RFC 7692 trailer
  }

  output.resize(compressed_size);
  deflateEnd(&strm);
  return {};
}

Expected compressBrotli(std::string_view input, std::vector<uint8_t> &output,
                        int quality) {
  // Clear the output vector
  output.clear();

  if (input.empty()) {
    return Expected{};
  }

  // Calculate maximum possible output size
  size_t max_output_size = BrotliEncoderMaxCompressedSize(input.size());
  if (max_output_size == 0) {
    return std::unexpected("Input too large for Brotli compression");
  }

  // Resize output buffer to maximum possible size
  output.resize(max_output_size);

  size_t actual_output_size = max_output_size;

  // Compress the data
  BROTLI_BOOL result = BrotliEncoderCompress(
      quality,               // quality (0-11, 6 is default)
      BROTLI_DEFAULT_WINDOW, // window size (10-24, default is 22)
      BROTLI_DEFAULT_MODE,   // mode (generic, text, font)
      input.size(),          // input size
      reinterpret_cast<const uint8_t *>(input.data()), // input data
      &actual_output_size,                             // output size (in/out)
      output.data()                                    // output buffer
  );

  if (result != BROTLI_TRUE) {
    output.clear();
    return std::unexpected("Brotli compression failed");
  }

  // Resize output to actual compressed size
  output.resize(actual_output_size);

  return Expected{};
}


Expected compressPayload(std::string_view input,
                                SupportedCompression compressionType,
                                std::vector<uint8_t> &output) {
  switch (compressionType) {
  case SupportedCompression::GZip:
  case SupportedCompression::HttpDeflate:
  case SupportedCompression::WSDeflate:
    return compressRFC7692(input, output, getWindowBits(compressionType));
  case SupportedCompression::Brotli:
    return compressBrotli(input, output);
  default:
    return std::unexpected("Unsupported compression type");
  }
}




Expected compressFileStreamingRFC7692(const std::filesystem::path &filePath,
                                      std::vector<uint8_t> &output,
                                      int windowBits) {
  if (!std::filesystem::exists(filePath) ||
      !std::filesystem::is_regular_file(filePath)) {
    return std::unexpected("File does not exist or is not a regular file");
  }

  std::ifstream file(filePath, std::ios::binary);
  if (!file) {
    return std::unexpected("Failed to open file for reading");
  }

  z_stream strm = {};
  int ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -windowBits,
                         8, Z_DEFAULT_STRATEGY);
  if (ret != Z_OK) {
    return std::unexpected("Failed to initialize DEFLATE");
  }

  output.clear();
  std::vector<uint8_t> inputBuffer(8192);  // 8KB input chunks
  std::vector<uint8_t> outputBuffer(8192); // 8KB output chunks

  while (file) {
    // Read chunk from file
    file.read(reinterpret_cast<char *>(inputBuffer.data()), inputBuffer.size());
    std::streamsize bytesRead = file.gcount();

    if (bytesRead == 0)
      break;

    strm.avail_in = bytesRead;
    strm.next_in = inputBuffer.data();

    // Compress chunk
    do {
      strm.avail_out = outputBuffer.size();
      strm.next_out = outputBuffer.data();

      int flush = file.eof() ? Z_SYNC_FLUSH : Z_NO_FLUSH;
      ret = deflate(&strm, flush);

      if (ret < 0) {
        deflateEnd(&strm);
        return std::unexpected("Compression failed during streaming");
      }

      size_t compressed = outputBuffer.size() - strm.avail_out;
      output.insert(output.end(), outputBuffer.begin(),
                    outputBuffer.begin() + compressed);

    } while (strm.avail_out == 0);
  }

  // RFC 7692: Remove trailing 0x00 0x00 0xFF 0xFF if present
  if (output.size() >= 4 && output[output.size() - 4] == 0x00 &&
      output[output.size() - 3] == 0x00 && output[output.size() - 2] == 0xFF &&
      output[output.size() - 1] == 0xFF) {
    output.resize(output.size() - 4);
  }

  deflateEnd(&strm);
  return {};
}

Expected compressFileStreamingBrotli(const std::filesystem::path &filePath,
                                     std::vector<uint8_t> &output,
                                     int quality) {
  if (!std::filesystem::exists(filePath) ||
      !std::filesystem::is_regular_file(filePath)) {
    return std::unexpected("File does not exist or is not a regular file");
  }

  std::ifstream file(filePath, std::ios::binary);
  if (!file) {
    return std::unexpected("Failed to open file for reading");
  }

  BrotliEncoderState *state =
      BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
  if (!state) {
    return std::unexpected("Failed to create Brotli encoder");
  }

  // Set quality parameter
  BrotliEncoderSetParameter(state, BROTLI_PARAM_QUALITY, quality);

  output.clear();
  std::vector<uint8_t> inputBuffer(8192);
  std::vector<uint8_t> outputBuffer(8192);

  while (file) {
    file.read(reinterpret_cast<char *>(inputBuffer.data()), inputBuffer.size());
    std::streamsize bytesRead = file.gcount();

    const uint8_t *input_ptr = inputBuffer.data();
    size_t input_size = bytesRead;
    bool is_last = file.eof();

    BrotliEncoderOperation op =
        is_last ? BROTLI_OPERATION_FINISH : BROTLI_OPERATION_PROCESS;

    while (input_size > 0 || (is_last && !BrotliEncoderIsFinished(state))) {
      uint8_t *output_ptr = outputBuffer.data();
      size_t output_size = outputBuffer.size();

      BROTLI_BOOL result =
          BrotliEncoderCompressStream(state, op, &input_size, &input_ptr,
                                      &output_size, &output_ptr, nullptr);

      if (!result) {
        BrotliEncoderDestroyInstance(state);
        return std::unexpected("Brotli streaming compression failed");
      }

      size_t compressed = outputBuffer.size() - output_size;
      if (compressed > 0) {
        output.insert(output.end(), outputBuffer.begin(),
                      outputBuffer.begin() + compressed);
      }

      if (input_size == 0 && !is_last)
        break;
    }
  }

  BrotliEncoderDestroyInstance(state);
  return {};
}

Expected compressFile(const std::filesystem::path &filePath,
                      SupportedCompression compressionType,
                      std::vector<uint8_t> &output) {
  switch (compressionType) {
  case SupportedCompression::GZip:
  case SupportedCompression::HttpDeflate:
  case SupportedCompression::WSDeflate:
    return compressFileStreamingRFC7692(filePath, output,
                                        getWindowBits(compressionType));
  case SupportedCompression::Brotli:
    return compressFileStreamingBrotli(filePath, output);
  default:
    return std::unexpected("Unsupported compression type");
  };

} 

}// namespace medici::http