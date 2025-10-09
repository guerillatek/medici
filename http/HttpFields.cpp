#include "http/HttpFields.hpp"
#include "http/MultipartPayload.hpp"
#include <expected>
#include <sstream>

namespace medici::http {

HttpFields::HttpFields(const MultipartPayload &multipartPayload) {
  _fieldValueMap = multipartPayload.getFormFields();
  _hasFilePathFields = multipartPayload.hasFileContent();
  for (auto &mapEntry : multipartPayload.getFilePathFields()) {
    _fieldValueMap.emplace(mapEntry.first,
                           FieldValueEntry{mapEntry.second.string(), true});
  }
}

Expected HttpFields::loadFromURLString(const std::string &urlEncodedFields) {
  _fieldValueMap =
      FieldContentUtils::parseURLEncodingToFields(urlEncodedFields);
  // Populate field names and array fields
  std::string lastFieldName;
  for (auto &mapEntry : _fieldValueMap) {
    if (mapEntry.first != lastFieldName) {
      ++_fieldCount;
    }
    lastFieldName = mapEntry.first;
  }
  return {};
}

std::expected<std::string, std::string>
HttpFields::getField(std::string fieldName) const {
  std::transform(fieldName.begin(), fieldName.end(), fieldName.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  auto entryResult = getFieldEntry(fieldName);
  if (!entryResult) {
    return std::unexpected(entryResult.error());
  }
  return entryResult.value()->second.value;
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
    values.push_back(fieldEntry->second.value);
    ++fieldEntry;
  }

  return values;
}

std::string HttpFields::encodeAsJSON() const {

  std::ostringstream encodedJSON;
  std::string arrayMemberContent;
  auto arrayFields = getArrayFields();
  auto activeArrayField = arrayFields.end();

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
    auto arrayField = arrayFields.find(mapEntry.first);
    if (arrayField != arrayFields.end()) {
      if (arrayField != activeArrayField) {
        if (activeArrayField != arrayFields.end()) {
          // We need to close off existing array
          encodedJSON << "]";
        }
        activeArrayField = arrayField;
        encodeMember(mapEntry.first, mapEntry.second.value, true);
        continue;
      }
      encodedJSON << ",\"" << mapEntry.second.value << '\"';
    } else {
      if (activeArrayField != arrayFields.end()) {
        activeArrayField = arrayFields.end();
        encodedJSON << "]";
      }
      encodeMember(mapEntry.first, mapEntry.second.value, false);
    }
  }
  if (activeArrayField != arrayFields.end()) {
    encodedJSON << "]";
  }
  encodedJSON << "}" << std::endl;
  return encodedJSON.str();
}

std::set<std::string> HttpFields::getFieldNames() const {
  std::set<std::string> fieldNames;
  for (const auto &[name, entry] : _fieldValueMap) {
    fieldNames.insert(name);
  }
  return fieldNames;
}

std::set<std::string> HttpFields::getArrayFields() const {

  std::set<std::string> arrayFields;
  for (const auto &[name, entry] : _fieldValueMap) {
    if (_fieldValueMap.count(name) > 1) {
      arrayFields.insert(name);
    }
  }
  return arrayFields;
}
} // namespace medici::http