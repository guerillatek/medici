#include "http/MultipartPayload.hpp"
#include "medici/http/HttpFields.hpp"
#include "medici/http/writeBufferToTempFile.hpp"
#include <fstream>
#include <sstream>

namespace medici::http {
ExpectedValue MultipartPayload::encodeToString() const {
  std::ostringstream payloadStream;
  for (const auto &[name, entry] : _formFields) {
    payloadStream << "--" << _boundary << "\r\n";
    payloadStream << "Content-Disposition: form-data; name=\"" << name
                  << "\"\r\n\r\n";
    payloadStream << entry.value << "\r\n";
  }
  for (const auto &[name, filePath] : _filePathFields) {
    payloadStream << "--" << _boundary << "\r\n";
    payloadStream << "Content-Disposition: form-data; name=\"" << name
                  << "\"; filename=\"" << filePath.filename().string()
                  << "\"\r\n";
    payloadStream << "Content-Type: application/octet-stream\r\n\r\n";
    // Read file content
    std::ifstream fileStream(filePath, std::ios::binary);
    if (!fileStream) {
      return std::unexpected("Failed to open file: " + filePath.string());
    }
    payloadStream << fileStream.rdbuf() << "\r\n";
  }
  payloadStream << "--" << _boundary << "--\r\n";
  return payloadStream.str();
}

std::string MultipartPayload::getActiveFileBoundaryHeader() {
  std::ostringstream headerStream;
  headerStream << "--" << _boundary << "\r\n";
  headerStream << "Content-Disposition: form-data; name=\""
               << _filePathFields.begin()->first << "\"; filename=\""
               << _filePathFields.begin()->second.filename().string()
               << "\"\r\n";
  headerStream << "Content-Type: application/octet-stream\r\n\r\n";
  return headerStream.str();
}

ExpectedValue MultipartPayload::partialEncodeNonFileToString() const {
  std::ostringstream payloadStream;
  for (const auto &[name, entry] : _formFields) {
    payloadStream << "--" << _boundary << "\r\n";
    payloadStream << "Content-Disposition: form-data; name=\"" << name
                  << "\"\r\n\r\n";
    payloadStream << entry.value << "\r\n";
  }
  return payloadStream.str();
}

Expected MultipartPayload::decodePayload(std::string_view payload) {
  std::string boundary = "--" + _boundary;
  size_t pos = 0;
  size_t end = payload.size();

  while (pos < end) {
    // Find the next boundary
    size_t boundaryPos = payload.find(boundary, pos);
    if (boundaryPos == std::string::npos) {
      break; // No more boundaries
    }
    pos = boundaryPos + boundary.size();

    // Check for the end boundary
    if (pos + 2 <= end && payload.substr(pos, 2) == "--") {
      break; // Reached the end boundary
    }

    // Skip CRLF after boundary
    if (pos + 2 <= end && payload.substr(pos, 2) == "\r\n") {
      pos += 2;
    }

    // Parse headers
    std::map<std::string, std::string> headers;
    while (true) {
      size_t lineEnd = payload.find("\r\n", pos);
      if (lineEnd == std::string::npos || lineEnd == pos) {
        pos += 2; // Skip the empty line
        break;    // End of headers
      }
      std::string line = std::string(payload.substr(pos, lineEnd - pos));
      size_t colonPos = line.find(':');
      if (colonPos != std::string::npos) {
        std::string headerName =
            line.substr(0, colonPos); // Header names are case-insensitive
        std::transform(headerName.begin(), headerName.end(), headerName.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        std::string headerValue = line.substr(colonPos + 1);
        // Trim leading whitespace from header value
        headerValue.erase(0, headerValue.find_first_not_of(" \t"));
        headers[headerName] = headerValue;
      }
      pos = lineEnd + 2; // Move to the next line
    }

    // Extract Content-Disposition header
    auto it = headers.find("CONTENT-DISPOSITION");
    if (it == headers.end()) {
      return std::unexpected("Missing Content-Disposition header");
    }
    std::string contentDisposition = it->second;

    // Parse Content-Disposition to extract name and filename
    std::string name;
    std::string filename;
    size_t namePos = contentDisposition.find("name=\"");
    if (namePos != std::string::npos) {
      size_t nameEnd = contentDisposition.find('"', namePos + 6);
      if (nameEnd != std::string::npos) {
        name = contentDisposition.substr(namePos + 6, nameEnd - (namePos + 6));
      }
    }
    size_t filenamePos = contentDisposition.find("filename=\"");
    if (filenamePos != std::string::npos) {
      size_t filenameEnd = contentDisposition.find('"', filenamePos + 10);
      if (filenameEnd != std::string::npos) {
        filename = contentDisposition.substr(filenamePos + 10,
                                             filenameEnd - (filenamePos + 10));
      }
    }
    // Find the next boundary to determine the end of the content
    size_t nextBoundaryPos = payload.find(boundary, pos);
    if (nextBoundaryPos == std::string::npos) {
      nextBoundaryPos = end;
    } else {
      nextBoundaryPos -= 2; // Move back before the CRLF
    }
    std::string content =
        std::string(payload.substr(pos, nextBoundaryPos - pos));
    // Remove trailing CRLF from content if present
    if (content.size() >= 2 && content.substr(content.size() - 2) == "\r\n") {
      content = content.substr(0, content.size() - 2);
    }
    pos = nextBoundaryPos;
    if (!filename.empty()) {
      // It's a file field
      // Write content to a temporary file
      std::string tempFilePath;
      try {
        tempFilePath = writeBufferToTempFile(content);
      } catch (const std::exception &e) {
        return std::unexpected("Failed to write file content to temp file: " +
                               std::string(e.what()));
      }
      _filePathFields.emplace(name, std::filesystem::path(tempFilePath));
    } else {
      // It's a regular form field
      _formFields.emplace(name, content);
    }
  }
  return Expected{};
}

} // namespace medici::http