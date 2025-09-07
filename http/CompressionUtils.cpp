
#include "CompressionUtils.hpp"

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
    return std::unexpected("RFC 7692: Use Z_SYNC_FLUSH for per-message compression failed");
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


Expected compressBrotli(std::string_view input, std::vector<uint8_t> &output, int quality) {
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
        quality,                                           // quality (0-11, 6 is default)
        BROTLI_DEFAULT_WINDOW,                            // window size (10-24, default is 22)
        BROTLI_DEFAULT_MODE,                              // mode (generic, text, font)
        input.size(),                                     // input size
        reinterpret_cast<const uint8_t*>(input.data()),   // input data
        &actual_output_size,                              // output size (in/out)
        output.data()                                     // output buffer
    );
    
    if (result != BROTLI_TRUE) {
        output.clear();
        return std::unexpected("Brotli compression failed");
    }
    
    // Resize output to actual compressed size
    output.resize(actual_output_size);
    
    return Expected{};
}

} // namespace medici::http