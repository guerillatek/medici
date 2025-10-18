#pragma once

#include <format>
#include <ostream>
#include <sstream>
#include <string>

namespace medici::http {

enum class ContentType {
  TextHtml,
  TextCSS,
  TextCSV,
  TextPlain,
  AppBinary,
  AppGZip,
  AppJavascript,
  AppJSON,
  AppXML,
  VideoFLV,
  VideoMPEG,
  VideoMP4,
  ImagePNG,
  ImageJPEG,
  ImageGIF,
  ImageBMP,
  ImageICO,
  ImageTIFF,
  ImageSVGXML,
  MutlipartForm,
  URLEncodedForm,
  Unspecified
};
const char *to_string(ContentType state);
inline std::ostream &operator<<(std::ostream &os, ContentType compression) {
  return os << to_string(compression);
}

enum class SupportedCompression : std::uint8_t {
  None = 0, // No compression
  GZip = 1, // RFC 1952
  // Note: HttpDeflate is the zlib format (RFC 1950) which
  // includes zlib headers and trailers.
  HttpDeflate = 2, // RFC 1950
  WSDeflate = 4,   // RFC 7692 - WebSocket DEFLATE
  // Brotli is a newer compression format that is not part of the original
  // HTTP/1.1 or HTTP/2 specifications, but is widely used in modern
  // web applications for its efficiency.
  // It is defined in RFC 7932.
  Brotli = 8 // RFC 7932
};

const char *to_string(SupportedCompression compression);
const char *to_encoding_value(SupportedCompression compression);

inline std::ostream &operator<<(std::ostream &os,
                                SupportedCompression compression) {
  return os << to_string(compression);
}

ContentType getContentTypeFromFilePath(const std::string &filePath);

} // namespace medici::http

template <> struct std::formatter<medici::http::ContentType> {

  constexpr auto parse(auto &ctx) { return ctx.begin(); }

  auto format(const medici::http::ContentType &action, auto &ctx) const {
    std::ostringstream writer;
    writer << action;
    return std::format_to(ctx.out(), "{}", writer.str());
  }
};

template <> struct std::formatter<medici::http::SupportedCompression> {

  constexpr auto parse(auto &ctx) { return ctx.begin(); }

  auto format(const medici::http::SupportedCompression &compression,
              auto &ctx) const {
    std::ostringstream writer;
    writer << compression;
    return std::format_to(ctx.out(), "{}", writer.str());
  }
};
