#include <expected>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

std::string writeStringToTempFile(const std::string &content,
                                  const std::string &prefix = "medici_",
                                  const std::string &suffix = ".tmp") {

  // Get system temp directory
  std::filesystem::path tempDir = std::filesystem::temp_directory_path();

  // Generate random filename
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(100000, 999999);

  std::string randomName = prefix + std::to_string(dis(gen)) + suffix;
  std::filesystem::path tempFilePath = tempDir / randomName;

  // Ensure file doesn't already exist (very unlikely but possible)
  while (std::filesystem::exists(tempFilePath)) {
    randomName = prefix + std::to_string(dis(gen)) + suffix;
    tempFilePath = tempDir / randomName;
  }

  // Write content to file
  std::ofstream file(tempFilePath, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to create temporary file: " +
                             tempFilePath.string());
  }

  file.write(content.data(), content.size());
  file.close();

  if (!file.good()) {
    // Clean up on write failure
    std::filesystem::remove(tempFilePath);
    throw std::runtime_error("Failed to write content to temporary file: " +
                             tempFilePath.string());
  }

  return tempFilePath;
}