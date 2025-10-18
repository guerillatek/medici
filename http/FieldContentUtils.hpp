#pragma once
#include "medici/http/HTTPAction.hpp"
#include <expected>
#include <map>
#include <ostream>

namespace medici::http {

using Expected = std::expected<void, std::string>;
using ExpectedValue = std::expected<std::string, std::string>;

struct FieldValueEntry {
  std::string value;
  bool isFilePath{false};

  friend bool operator<<(std::ostream &os, const FieldValueEntry &entry) {
    os << entry.value;
    return true;
  }
};

using FieldValueMap = std::multimap<std::string, FieldValueEntry>;

// Equality operator for FieldValueMap to support unit tests
bool operator==(const FieldValueMap &lhs, const FieldValueMap &rhs);

class FieldContentUtils {
public:
  static FieldValueMap parseURLEncodingToFields(std::string);
  static ExpectedValue encodeFieldsToURL(const FieldValueMap &);
  static ExpectedValue encodeFieldsToRequestHeader(HTTPAction action,
                                                   const std::string &uriPath,
                                                   const std::string &host,
                                                   const FieldValueMap &values);
  static ExpectedValue
  encodeFieldsToResponseHeader(int responseCode, const std::string &message,
                               const FieldValueMap &values);
  static std::string encodeStringForURL(const std::string &str);
  static std::string decodeStringFromURL(const std::string &str);

private:
  static Expected loadHeaderValues(std::ostringstream &contentHeader,
                                   const FieldValueMap &values);
};

} // namespace medici::http