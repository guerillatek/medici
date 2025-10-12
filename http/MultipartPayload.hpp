#pragma once

#include "medici/http/FieldContentUtils.hpp"
#include "medici/http/HTTPAction.hpp"
#include "medici/http/HttpFields.hpp"
#include <filesystem>
namespace medici::http {

class MultipartPayload {
public:
  explicit MultipartPayload(const HttpFields &fields,
                            std::string boundaryPrefix = "mediciHttp")
      : _boundary{boundaryPrefix + std::to_string(std::rand())} {
    fields.segregateMultipartFields(_formFields, _filePathFields);
  }
  MultipartPayload() = default;

  std::string getBoundary() const { return _boundary; }
  std::string getContentType() const {
    return "multipart/form-data; boundary=" + _boundary;
  }

  ExpectedValue encodeToString() const;
  ExpectedValue partialEncodeNonFileToString() const;
  using ExpectedPath = std::expected<std::filesystem::path, std::string>;
  ExpectedPath getActiveFile() const {
    if (_filePathFields.empty()) {
      return std::unexpected("No active file in multipart payload");
    }
    return _filePathFields.begin()->second;
  }
  std::string getActiveFileBoundaryHeader();
  void removeActiveFile() {
    if (!_filePathFields.empty()) {
      _filePathFields.erase(_filePathFields.begin());
    }
  }

  std::string getTailBoundary() const { return "--" + _boundary + "--\r\n"; }

  bool hasFileContent() const { return !_filePathFields.empty(); }

  auto &getFilePathFields() const { return _filePathFields; }
  auto &getFormFields() const { return _formFields; }

  Expected decodePayload(const std::string_view, const std::string &boundary);

private:
  std::string _boundary{};
  FieldValueMap _formFields{};
  using FileFieldValueMap = std::multimap<std::string, std::filesystem::path>;
  FileFieldValueMap _filePathFields{};
};

} // namespace medici::http
