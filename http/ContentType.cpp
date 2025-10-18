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

const char *to_encoding_value(SupportedCompression compression) {
  switch (compression) {
  case SupportedCompression::GZip:
    return "gzip";
  case SupportedCompression::HttpDeflate:
    return "deflate";
  case SupportedCompression::Brotli:
    return "br";
  default:
    break;
  }
  return ""; // No compression
}

ContentType getContentTypeFromFilePath(const std::string &filePath) {
  auto dotPos = filePath.rfind('.');
  if (dotPos == std::string::npos || dotPos == filePath.length() - 1) {
    return ContentType::Unspecified;
  }

  std::string extension = filePath.substr(dotPos + 1);
  if (extension == "html" || extension == "htm") {
    return ContentType::TextHtml;
  } else if (extension == "css") {
    return ContentType::TextCSS;
  } else if (extension == "csv") {
    return ContentType::TextCSV;
  } else if (extension == "txt") {
    return ContentType::TextPlain;
  } else if (extension == "js") {
    return ContentType::AppJavascript;
  } else if (extension == "json") {
    return ContentType::AppJSON;
  } else if (extension == "xml") {
    return ContentType::AppXML;
  } else if (extension == "png") {
    return ContentType::ImagePNG;
  } else if (extension == "jpg" || extension == "jpeg") {
    return ContentType::ImageJPEG;
  } else if (extension == "gif") {
    return ContentType::ImageGIF;
  } else if (extension == "bmp") {
    return ContentType::ImageBMP;
  } else if (extension == "ico") {
    return ContentType::ImageICO;
  } else if (extension == "tiff" || extension == "tif") {
    return ContentType::ImageTIFF;
  } else if (extension == "svg") {
    return ContentType::ImageSVGXML;
  } else if (extension == "gz") {
    return ContentType::AppGZip;
  } else if (extension == "mp4") {
    return ContentType::VideoMP4;
  } else if (extension == "flv") {
    return ContentType::VideoFLV;
  } else if (extension == "mpeg" || extension == "mpg") {
    return ContentType::VideoMPEG;
  }

  return ContentType::Unspecified;
}

} // namespace medici::http