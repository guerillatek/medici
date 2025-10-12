#pragma once

#include "medici/http/FieldContentUtils.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <set>
#include <string>
#include <vector>
namespace medici::http {
class MultipartPayload;
class HttpFields {
public:
  explicit HttpFields(const MultipartPayload &multipartPayload);
  explicit HttpFields(const FieldValueMap &multipartPayload);
  HttpFields() = default;
  ~HttpFields() = default;
  HttpFields(const HttpFields &other) = default;
  Expected loadFromURLString(const std::string &urlEncodedFields);
  std::uint32_t getFieldCount() const { return _fieldCount; }
  std::set<std::string> getFieldNames() const;
  std::set<std::string> getArrayFields() const;
  std::expected<std::string, std::string> getField(std::string) const;
  std::expected<std::vector<std::string>, std::string>
      getArrayFieldValues(std::string) const;

  auto HasField(std::string fieldName) const {
    std::transform(fieldName.begin(), fieldName.end(), fieldName.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    auto fieldEntry = _fieldValueMap.find(fieldName);
    return fieldEntry != _fieldValueMap.end();
  }

  void addFieldValue(std::string fieldName, const std::string &value,
                     bool isFilePath = false) {
    std::transform(fieldName.begin(), fieldName.end(), fieldName.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    if (_fieldValueMap.contains(fieldName)) {
      _hasArrayFields = true;
    } else {
      ++_fieldCount;
    }
    _fieldValueMap.emplace(fieldName, FieldValueEntry{value, isFilePath});
    if (isFilePath) {
      _hasFilePathFields = true;
    } 
  }

  auto encodeAsQueryString() const {
    return FieldContentUtils::encodeFieldsToURL(_fieldValueMap);
  }

  auto encodeFieldsToResponseHeader(int responseCode,
                                    const std::string &message) {
    return FieldContentUtils::encodeFieldsToResponseHeader(
        responseCode, message, _fieldValueMap);
  }

  auto encodeFieldsToRequestHeader(HTTPAction action,
                                   const std::string &uriPath,
                                   const std::string &host) {
    return FieldContentUtils::encodeFieldsToRequestHeader(action, uriPath, host,
                                                          _fieldValueMap);
  }

  void segregateMultipartFields(auto &formFields, auto &filePaths) const {
    for (const auto &[name, entry] : _fieldValueMap) {
      if (entry.isFilePath) {
        filePaths.insert({name, std::filesystem::path(entry.value)});
      } else {
        formFields.insert({name, entry});
      }
    }
  }

  std::string encodeAsJSON() const;

  void clear() {

    _fieldValueMap.clear();
    _fieldCount = 0;
    _hasArrayFields = false;
  }
  bool hasFilePathFields() const { return _hasFilePathFields; }

  bool operator==(const HttpFields &other) const {
    return _fieldValueMap == other._fieldValueMap;
  }

protected:
  void updateFieldCountAndArrayFlags();
  std::expected<FieldValueMap::const_iterator, std::string>
  getFieldEntry(std::string fieldName) const {
    std::transform(fieldName.begin(), fieldName.end(), fieldName.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    auto fieldEntry = _fieldValueMap.find(fieldName);
    if (fieldEntry == _fieldValueMap.end()) {
      return std::unexpected(
          std::format("Attempted to get unknown form field,'{}'", fieldName));
    }
    return fieldEntry;
  }

  FieldValueMap _fieldValueMap{};
  std::uint32_t _fieldCount{0};
  bool _hasArrayFields{false};
  bool _hasFilePathFields{false};
};

} // namespace medici::http