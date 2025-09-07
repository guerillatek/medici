#pragma once
#include "medici/http/HTTPAction.hpp"
#include <expected>
#include <map>

namespace medici::http {

using Expected = std::expected<void, std::string>;

using FieldValueMap = std::multimap<std::string, std::string>;
class FieldContentUtils {
public:
  static FieldValueMap parseURLEncoding(const std::string &);
  static std::string encodeFieldsToURL(const FieldValueMap &);
  static std::string encodeFieldsToRequestHeader(HTTPAction action,
                                                 const std::string &uriPath,
                                                 const std::string &host,
                                                 const FieldValueMap &values);
  static std::string encodeFieldsToResponseHeader(int responseCode,
                                                  const std::string &message,
                                                  const FieldValueMap &values);
  static std::string encodeStringForURL(const std::string &str);

private:
  static void loadHeaderValues(std::ostringstream &contentHeader,
                               const FieldValueMap &values);
};

} // namespace medici::http