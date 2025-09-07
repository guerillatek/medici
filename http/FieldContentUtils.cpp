#include "medici/http/FieldContentUtils.hpp"
#include <format>
#include <iomanip>
#include <regex>
#include <sstream>
#include <string>

namespace medici::http {
static std::regex FieldValue(R"([a-zA-Z0-9\.\-\~\_]+)");
static std::regex Space(R"(\+)");
static std::regex SpecialCharHexVal(R"(\%[0-9A-F][0-9A-F])");
static std::regex Equal(R"(\=)");
static std::regex Ampersand(R"(\&)");

FieldValueMap FieldContentUtils::parseURLEncoding(const std::string &content) {
  std::string fieldName;
  std::string fieldValue;
  bool scanningField = true;
  FieldValueMap fieldValueMap;

  while (!content.empty()) {
    auto matchFunction = [](std::string fieldContent,
                            std::regex &expression) -> std::string {
      std::smatch match;
      regex_search(fieldContent, match, expression);
      if (!match.size()) {
        return std::string{};
      }
      const auto &matchContent = match.str(0);
      if (fieldContent.compare(0, matchContent.size(), matchContent) == 0) {
        // We have a valid match, update content to exclude it
        fieldContent = fieldContent.substr(match.str(0).size());
        return matchContent;
      }
      return std::string{};
    };

    if (auto match = matchFunction(content, FieldValue);
        !match.empty()) { // Amend the current field name or value
      if (scanningField) {
        fieldName += match;
      } else {
        fieldValue += match;
      }
    } else if (match = matchFunction(content, SpecialCharHexVal);
               !match.empty()) {
      // We've detected a special char represented as hex value
      // that is apart of either the field or value
      int specialCharVal = 0;
      int msd = match[1];
      int lsd = match[2];
      if ((msd >= 'A') && (msd <= 'F'))
        specialCharVal = (msd - 'A' + 10) * 16;
      if ((msd >= '0') && (msd <= '9'))
        specialCharVal = (msd - '0') * 16;
      if ((lsd >= 'A') && (lsd <= 'F'))
        specialCharVal += (lsd - 'A' + 10);
      if ((lsd >= '0') && (lsd <= '9'))
        specialCharVal += (lsd - '0');

      if (scanningField) {
        fieldName.append(1, static_cast<char>(specialCharVal));
      } else {
        fieldValue.append(1, static_cast<char>(specialCharVal));
      }
    } else if (match = matchFunction(content, Space); !match.empty()) {
      // We've detected a space represented as "+"
      // that is apart of either the field or value
      if (scanningField) {
        fieldName += " ";
      } else {
        fieldValue += " ";
      }
    } else if (match = matchFunction(content, Equal);
               !match.empty()) { // transition to parsing the field value
      scanningField = false;
    } else if (match = matchFunction(content, Ampersand); !match.empty()) {
      // We're about to start a new name value pair
      // so add the current field name and value to map
      fieldValueMap.insert({fieldName, fieldValue});
      fieldName.clear();
      fieldValue.clear();
      scanningField = true;
    } else {
      break;
    }
  }
  // We're at the end of our parsing so add the last or
  // only field name and value to map
  if ((!fieldName.empty()) && (!fieldValue.empty())) {
    fieldValueMap.insert({fieldName, fieldValue});
  }
  return fieldValueMap;
}

std::string FieldContentUtils::encodeStringForURL(const std::string &str) {
  std::ostringstream encodedString;
  encodedString.fill('0');
  encodedString << std::hex;

  for (char ch : str) {
    if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      encodedString << ch;
    } else {
      encodedString << std::uppercase;
      encodedString << '%' << std::setw(2) << static_cast<int>(ch);
      encodedString << std::nouppercase;
    }
  }

  return encodedString.str();
}

std::string FieldContentUtils::encodeFieldsToURL(const FieldValueMap &values) {
  std::ostringstream encodedURL;
  for (auto &[name, value] : values) {
    if (!encodedURL.str().empty()) {
      encodedURL << '&';
    }
    encodedURL << encodeStringForURL(name) << '=' << encodeStringForURL(value);
  }
  return encodedURL.str();
}

std::string FieldContentUtils::encodeFieldsToRequestHeader(
    HTTPAction action, const std::string &uriPath, const std::string &host,
    const FieldValueMap &values) {
  std::ostringstream contentHeader;
  contentHeader << std::format("{} {} HTTP/1.1\r\n", action, uriPath);
  contentHeader << std::format("Host: {}", host);
  loadHeaderValues(contentHeader, values);
  return contentHeader.str();
}

std::string FieldContentUtils::encodeFieldsToResponseHeader(
    int responseCode, const std::string &message, const FieldValueMap &values) {
  std::ostringstream contentHeader;
  contentHeader << std::format("HTTP/1.1 {} {}\r\n", responseCode, message);
  loadHeaderValues(contentHeader, values);
  return contentHeader.str();
}

void FieldContentUtils::loadHeaderValues(std::ostringstream &contentHeader,
                                         const FieldValueMap &values) {
  std::string activeName;
  for (const auto &[name, value] : values) {
    if (name != activeName) {
      contentHeader << "\r\n" << name << ": ";
      activeName = name;
    } else {
      contentHeader << "; ";
    }
    contentHeader << value;
  }
  contentHeader << "\r\n\r\n";
}

} // namespace medici::http