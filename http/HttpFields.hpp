#pragma once

#include "medici/http/FieldContentUtils.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <expected>
#include <format>
#include <set>
#include <string>
#include <vector>

namespace medici::http {

class HttpFields {
public:
  HttpFields() {};
  HttpFields(FieldValueMap &&);
  HttpFields(const std::string &urlEncodedFields)
      : HttpFields{FieldContentUtils::parseURLEncoding(urlEncodedFields)} {}
  std::uint32_t getFieldCount() const { return _fieldCount; }
  const std::set<std::string> &getFieldNames() { return _fieldNames; }
  const std::set<std::string> &getArrayFields() const { return _arrayFields; }
  std::expected<std::string, std::string> getField(std::string) const;
  std::expected<std::vector<std::string>, std::string>
      getArrayFieldValues(std::string) const;

  auto HasField(std::string fieldName) const {
    std::transform(fieldName.begin(), fieldName.end(), fieldName.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    auto fieldEntry = _fieldValueMap.find(fieldName);
    return fieldEntry != _fieldValueMap.end();
  }

  void addFieldValue(std::string fieldName, const std::string &value) {
    std::transform(fieldName.begin(), fieldName.end(), fieldName.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    if (_fieldNames.contains(fieldName)) {
      _arrayFields.insert(fieldName);
      _hasArrayFields = true;
    } else {
      ++_fieldCount;
    }
    _fieldValueMap.emplace(fieldName, value);
  }

  auto encodeAsQueryString() const {
    return FieldContentUtils::encodeFieldsToURL(_fieldValueMap);
  }

  std::string encodeFieldsToRequestHeader(HTTPAction action,
                                          const std::string &uriPath,
                                          const std::string &host) {
    return FieldContentUtils::encodeFieldsToRequestHeader(action, uriPath, host,
                                                          _fieldValueMap);
  }

  std::string encodeFieldsToResponseHeader(int responseCode,
                                         const std::string &message) {
    return FieldContentUtils::encodeFieldsToResponseHeader(
        responseCode, message, _fieldValueMap);
  }

  std::string encodeAsJSON() const;

  void clear() {

    _fieldValueMap.clear();
    _fieldCount = 0;
    _hasArrayFields = false;
    _fieldNames.clear();
    _arrayFields.clear();
  }

protected:
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
  std::set<std::string> _fieldNames{};
  std::set<std::string> _arrayFields{};
};

} // namespace medici::http