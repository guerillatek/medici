#include "http/HttpFields.hpp"

#include <expected>
#include <sstream>

namespace medici::http {

HttpFields::HttpFields(FieldValueMap &&fieldValueMap)
    : _fieldValueMap{std::move(fieldValueMap)} {
  std::string lastFieldName;
  for (auto &mapEntry : _fieldValueMap) {
    if (mapEntry.first == lastFieldName) {
      _arrayFields.insert(lastFieldName);
    } else {
      ++_fieldCount;
    }
    lastFieldName = mapEntry.first;
    _fieldNames.insert(mapEntry.first);
  }
}

std::expected<std::string, std::string>
HttpFields::getField(std::string fieldName) const {
  std::transform(fieldName.begin(), fieldName.end(), fieldName.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  auto result = getFieldEntry(fieldName);
  if (!result) {
    return std::unexpected(result.error());
  }
  return result.value()->second;
}

std::expected<std::vector<std::string>, std::string>
HttpFields::getArrayFieldValues(std::string fieldName) const {
  std::transform(fieldName.begin(), fieldName.end(), fieldName.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  std::vector<std::string> values;
  auto result = getFieldEntry(fieldName);
  if (!result) {
    return std::unexpected(result.error());
  }

  auto fieldEntry = result.value();
  while ((fieldEntry != _fieldValueMap.end()) &&
         fieldEntry->first == fieldName) {
    values.push_back(fieldEntry->second);
    ++fieldEntry;
  }

  return values;
}

std::string HttpFields::encodeAsJSON() const {

  std::ostringstream encodedJSON;
  std::string arrayMemberContent;
  auto activeArrayField = _arrayFields.end();

  encodedJSON << "{";

  auto encodeMember = [&](const std::string &member, const std::string content,
                          bool startArray) {
    char quote = '\"';
    if (encodedJSON.str().size() > 1) {
      encodedJSON << ",";
    }
    encodedJSON << "\"" << member << "\":" << ((startArray) ? "[" : "") << quote
                << content << quote;
  };

  for (auto &mapEntry : _fieldValueMap) {
    auto arrayField = _arrayFields.find(mapEntry.first);
    if (arrayField != _arrayFields.end()) {
      if (arrayField != activeArrayField) {
        if (activeArrayField != _arrayFields.end()) {
          // We need to close off existing array
          encodedJSON << "]";
        }
        activeArrayField = arrayField;
        encodeMember(mapEntry.first, mapEntry.second, true);
        continue;
      }
      encodedJSON << ",\"" << mapEntry.second << "\"";
    } else {
      if (activeArrayField != _arrayFields.end()) {
        activeArrayField = _arrayFields.end();
        encodedJSON << "]";
      }
      encodeMember(mapEntry.first, mapEntry.second, false);
    }
  }
  if (activeArrayField != _arrayFields.end()) {
    encodedJSON << "]";
  }
  encodedJSON << "}" << std::endl;
  return encodedJSON.str();
}

} // namespace medici::http