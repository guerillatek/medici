#pragma once

#include "medici/http/ContentType.hpp"
#include <brotli/decode.h>
#include <brotli/encode.h>

#include "medici/http/writeBufferToTempFile.hpp"
#include <expected>
#include <filesystem>
#include <string>
#include <vector>
#include <zlib.h>

namespace medici::http {

using Expected = std::expected<void, std::string>;

inline int getWindowBits(SupportedCompression compressionType) {
  switch (compressionType) {
  case SupportedCompression::GZip:
    return 15 + 16; // gzip format
  case SupportedCompression::HttpDeflate:
    return 15; // zlib format
  case SupportedCompression::WSDeflate:
    return -15; // raw DEFLATE for WebSocket RFC 7692
  case SupportedCompression::Brotli:
    return BROTLI_DEFAULT_WINDOW; // Brotli uses its own window size
  default:
    return 0;
  }
}

// Compression utility functions

Expected compressRFC7692(std::string_view input, std::vector<uint8_t> &output,
                         int windowBits);

Expected compressBrotli(std::string_view input, std::vector<uint8_t> &output,
                        int quality = 6);

Expected compressPayload(std::string_view input,
                         SupportedCompression compressionType,
                         std::vector<uint8_t> &output);

Expected compressFileStreamingBrotli(const std::filesystem::path &filePath,
                                     std::vector<uint8_t> &output,
                                     int quality = 6);

Expected compressFileStreamingRFC7692(const std::filesystem::path &filePath,
                                      std::vector<uint8_t> &output,
                                      int windowBits = 15);

Expected compressFile(const std::filesystem::path &filePath,
                      SupportedCompression compressionType,
                      std::vector<uint8_t> &output);
// Decompression utility functions

template <typename T>
concept RawPayloadHandlerC = requires(T t) {
  { t(std::string_view{}) } -> std::same_as<Expected>;
};

Expected
decompressZlibBasedStreaming(std::string_view compressedPayload,
                             SupportedCompression compressionType,
                             RawPayloadHandlerC auto &&partialPayloadHandler) {

  z_stream strm = {};

  auto windowBits = getWindowBits(compressionType);
  if (windowBits == 0) {
    return std::unexpected("Unsupported compression type");
  }

  if (inflateInit2(&strm, windowBits) != Z_OK) {
    return std::unexpected("Failed to initialize decompression");
  }

  std::vector<uint8_t> buffer(8192); // 8KB chunks

  strm.avail_in = compressedPayload.size();
  strm.next_in =
      reinterpret_cast<Bytef *>(const_cast<char *>(compressedPayload.data()));

  int ret;
  do {
    strm.avail_out = buffer.size();
    strm.next_out = buffer.data();

    ret = inflate(&strm, Z_NO_FLUSH);

    if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
      inflateEnd(&strm);
      return std::unexpected("Decompression failed");
    }

    size_t decompressed = buffer.size() - strm.avail_out;
    if (decompressed > 0) {
      std::string_view chunk(reinterpret_cast<char *>(buffer.data()),
                             decompressed);
      auto result = partialPayloadHandler(chunk);
      if (!result) {
        inflateEnd(&strm);
        return std::unexpected("Handler failed: " + result.error());
      }
    }

  } while (ret != Z_STREAM_END && strm.avail_out == 0);

  inflateEnd(&strm);
  return Expected{}; // Indicate success
}

Expected decompressBrotliStreaming(std::string_view compressed,
                                   auto &&handler) {
  BrotliDecoderState *state =
      BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
  if (!state) {
    return std::unexpected("Failed to create Brotli decoder");
  }

  const uint8_t *input = reinterpret_cast<const uint8_t *>(compressed.data());
  size_t input_size = compressed.size();

  std::vector<uint8_t> outputBuffer(8192);

  while (input_size > 0) {
    uint8_t *output = outputBuffer.data();
    size_t output_size = outputBuffer.size();

    BrotliDecoderResult result = BrotliDecoderDecompressStream(
        state, &input_size, &input, &output_size, &output, nullptr);

    size_t decoded = outputBuffer.size() - output_size;
    if (decoded > 0) {
      std::string_view chunk(reinterpret_cast<char *>(outputBuffer.data()),
                             decoded);
      auto handler_result = handler(chunk);
      if (!handler_result) {
        BrotliDecoderDestroyInstance(state);
        return handler_result;
      }
    }

    if (result == BROTLI_DECODER_RESULT_SUCCESS) {
      break;
    } else if (result == BROTLI_DECODER_RESULT_ERROR) {
      BrotliDecoderDestroyInstance(state);
      return std::unexpected("Brotli decompression error");
    }
    return {};
  }

  BrotliDecoderDestroyInstance(state);
  return Expected{}; // Indicate success
}

Expected decompressPayloadToPartialPayloadHandler(
    std::string_view compressedPayload, SupportedCompression compressionType,
    RawPayloadHandlerC auto &&partialPayloadHandler) {
  switch (compressionType) {
  case SupportedCompression::GZip:
  case SupportedCompression::HttpDeflate:
  case SupportedCompression::WSDeflate:
    return decompressZlibBasedStreaming(compressedPayload, compressionType,
                                        partialPayloadHandler);
  case SupportedCompression::Brotli:
    return decompressBrotliStreaming(compressedPayload, partialPayloadHandler);
  default:
    return std::unexpected("Unsupported compression type");
  }
}

Expected decompressPayloadToBuffer(std::string_view compressedPayload,
                                   SupportedCompression compressionType,
                                   auto &targetBuffer) {
  return decompressPayloadToPartialPayloadHandler(
      compressedPayload, compressionType,
      [&targetBuffer](std::string_view chunk) {
        std::copy(chunk.begin(), chunk.end(), std::back_inserter(targetBuffer));
        return Expected{};
      });
}

} // namespace medici::http