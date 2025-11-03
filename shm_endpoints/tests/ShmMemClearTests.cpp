#include "medici/shm_endpoints/SharedMemoryObjectManager.hpp"
#include <cstring>
#include <iostream>

struct TestStruct {
  int value;
  char data[64];

  TestStruct(int v = 0) : value(v) {
    std::memset(data, 0, sizeof(data));
    std::snprintf(data, sizeof(data), "TestStruct_%d", v);
  }
};

int main() {
  const std::string shm_name = "test_size_and_clear";

  std::cout << "=== Testing Shared Memory Size and Clear Functionality ==="
            << std::endl;

  try {
    // First, create a segment with initial size
    {
      std::cout << "\n1. Creating initial segment with size 1024 bytes..."
                << std::endl;
      medici::shm_endpoints::SharedMemoryObjectManager<TestStruct> mgr1(
          shm_name, 1024, 42);

      std::cout << "Created: " << mgr1.created() << std::endl;
      std::cout << "Size: " << mgr1.size() << " bytes" << std::endl;
      std::cout << "Value: " << mgr1->value << std::endl;
      std::cout << "Data: " << mgr1->data << std::endl;

      // Fill some memory to test clearing
      std::memset(reinterpret_cast<char *>(&(*mgr1)) + sizeof(TestStruct), 0xFF,
                  100);
    } // mgr1 goes out of scope but segment remains

    // Open existing segment and try to resize it
    {
      std::cout << "\n2. Opening existing segment with larger size (2048) - "
                   "should always clear..."
                << std::endl;
      medici::shm_endpoints::SharedMemoryObjectManager<TestStruct> mgr2(
          shm_name, 2048, 100);

      std::cout << "Created: " << mgr2.created() << std::endl;
      std::cout << "Size: " << mgr2.size() << " bytes" << std::endl;
      std::cout << "Value: " << mgr2->value << std::endl;
      std::cout << "Data: " << mgr2->data << std::endl;

      // Check if memory after the struct was cleared
      char *after_struct =
          reinterpret_cast<char *>(&(*mgr2)) + sizeof(TestStruct);
      bool is_cleared = true;
      for (int i = 0; i < 100; i++) {
        if (after_struct[i] != 0) {
          is_cleared = false;
          break;
        }
      }
      std::cout << "Memory after struct is cleared: "
                << (is_cleared ? "YES" : "NO") << std::endl;
    }

    // Test opening with same size - should still clear
    {
      std::cout << "\n3. Opening with same size (2048) - should still clear..."
                << std::endl;

      // First, put some data there using open-only mode
      {
        medici::shm_endpoints::SharedMemoryObjectManager<TestStruct> mgr_setup(
            shm_name, 2048);
        char *after_struct =
            reinterpret_cast<char *>(&(*mgr_setup)) + sizeof(TestStruct);
        std::memset(after_struct, 0xAA, 100);
        std::cout << "Set memory pattern to 0xAA in existing segment"
                  << std::endl;
      }

      // Now open with size specified - should clear
      medici::shm_endpoints::SharedMemoryObjectManager<TestStruct> mgr3(
          shm_name, 2048, 300);

      std::cout << "Created: " << mgr3.created() << std::endl;
      std::cout << "Size: " << mgr3.size() << " bytes" << std::endl;
      std::cout << "Value: " << mgr3->value << std::endl;

      // Check if memory after the struct was cleared
      char *after_struct =
          reinterpret_cast<char *>(&(*mgr3)) + sizeof(TestStruct);
      bool is_cleared = true;
      for (int i = 0; i < 100; i++) {
        if (after_struct[i] != 0) {
          is_cleared = false;
          break;
        }
      }
      std::cout << "Memory cleared (size specified): "
                << (is_cleared ? "YES" : "NO") << std::endl;
    }

    // Test with different size - should clear and resize
    {
      std::cout << "\n4. Opening with different size (3072) - should clear and "
                   "resize..."
                << std::endl;
      medici::shm_endpoints::SharedMemoryObjectManager<TestStruct> mgr4(
          shm_name, 3072, 400);

      std::cout << "Created: " << mgr4.created() << std::endl;
      std::cout << "Size: " << mgr4.size() << " bytes" << std::endl;
      std::cout << "Value: " << mgr4->value << std::endl;

      // Check if memory was cleared
      char *after_struct =
          reinterpret_cast<char *>(&(*mgr4)) + sizeof(TestStruct);
      bool is_cleared = true;
      for (int i = 0; i < 100; i++) {
        if (after_struct[i] != 0) {
          is_cleared = false;
          break;
        }
      }
      std::cout << "Memory cleared and resized: " << (is_cleared ? "YES" : "NO")
                << std::endl;
    }

    // Test open-only mode (no clearing)
    {
      std::cout << "\n5. Testing open-only mode (no size, no clearing)..."
                << std::endl;

      // First put some data in using size constructor

      medici::shm_endpoints::SharedMemoryObjectManager<TestStruct> mgr_setup(
          shm_name, 3072, 500);
      char *after_struct_0 =
          reinterpret_cast<char *>(&(*mgr_setup)) + sizeof(TestStruct);
      std::memset(after_struct_0, 0xBB, 100);
      std::cout << "Set memory pattern to 0xBB" << std::endl;

      // Now open with open-only mode (no size specified)
      medici::shm_endpoints::SharedMemoryObjectManager<TestStruct> mgr5(
          shm_name);

      std::cout << "Size: " << mgr5.size() << " bytes" << std::endl;
      std::cout << "Value: " << mgr5->value << std::endl;

      // Check if memory pattern was preserved
      char *after_struct =
          reinterpret_cast<char *>(&(*mgr5)) + sizeof(TestStruct);
      bool has_pattern = true;
      for (int i = 0; i < 100; i++) {
        if (static_cast<unsigned char>(after_struct[i]) != 0xBB) {
          has_pattern = false;
          break;
        }
      }
      std::cout << "Memory pattern preserved (open-only): "
                << (has_pattern ? "YES" : "NO") << std::endl;
    }

    std::cout << "\n=== Test completed successfully ===" << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  // Clean up
  shm_unlink(shm_name.c_str());
  return 0;
}