#include "ContentType.hpp"

namespace medici::http {
const char *to_string(ContentType state) {
  switch (state) {
  case ContentType::TextHtml:
    return "text/html";
  case ContentType::TextCSS:
    return "text/css";
  case ContentType::TextCSV:
    return "text/csv";
  case ContentType::TextPlain:
    return "text/plain";
  case ContentType::AppBinary:
    return "application/octet-stream";
  case ContentType::AppGZip:
    return "application/gzip";
  case ContentType::AppJavascript:
    return "application/javascript";
  case ContentType::AppJSON:
    return "application/json";
  case ContentType::AppXML:
    return "application/xml";
  case ContentType::VideoFLV:
    return "video/x-flv";
  case ContentType::VideoMPEG:
    return "video/mpeg";
  case ContentType::VideoMP4:
    return "video/mp4";
  case ContentType::ImagePNG:
    return "image/png";
  case ContentType::ImageJPEG:
    return "image/jpeg";
  case ContentType::ImageGIF:
    return "image/gif";
  case ContentType::ImageBMP:
    return "image/bmp";
  case ContentType::ImageICO:
    return "image/vnd.microsoft.icon";
  case ContentType::ImageTIFF:
    return "image/tiff";
  case ContentType::ImageSVGXML:
    return "image/svg+xml";
  case ContentType::MutlipartForm:
    return "multipart/form-data";
  case ContentType::URLEncodedForm:
    return "application/x-www-form-urlencoded";
  case ContentType::Unspecified:
    return "application/unspecified";
  }
  return "application/unknown"; // Fallback
}

const char *to_string(SupportedCompression compression) {
  switch (compression) {
  case SupportedCompression::GZip:
    return "GZip";
  case SupportedCompression::HttpDeflate:
    return "Deflate";
  case SupportedCompression::WSDeflate:
    return "WSDeflate";
  case SupportedCompression::Brotli:
    return "Brotli";
  };
  return "Invalid Compression Value"; // Fallback
}
} // namespace medici::http