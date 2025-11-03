#include "medici/shm_endpoints/SharedMemPODQueueDefinition.hpp"
#include "medici/shm_endpoints/SharedMemoryObjectManager.hpp"
#include <cassert>
#include <iostream>

struct TestMessage {
  int value;
  char data[32];
};

using TestQueueDef =
    medici::shm_endpoints::SharedMemPODQueueDefinition<TestMessage>;
using TestLayout = TestQueueDef::SharedMemoryLayout;

int main() {
  const std::string shm_name = "ShmUniqueNametest";
  const std::uint32_t max_channels = 3;
  const std::uint32_t queue_size = 5;

  try {
    // Create shared memory with the layout
    medici::shm_endpoints::SharedMemoryObjectManager<TestLayout> shm_mgr(
        shm_name,
        sizeof(TestLayout) +
            max_channels *
                TestQueueDef::QueueChannelT::getQueueSize(queue_size),
        max_channels, queue_size);

    std::cout << "=== Testing Channel Name Validation ===" << std::endl;

    auto &layout = *shm_mgr;

    // Test 1: Allocate channels with unique names
    std::cout << "\n1. Allocating channels with unique names:" << std::endl;

    auto &channel1 = layout.AllocQueueChannel("consumer_a");
    std::cout << "   Channel 1 allocated: '" << channel1.name << "'"
              << std::endl;

    auto &channel2 = layout.AllocQueueChannel("consumer_b");
    std::cout << "   Channel 2 allocated: '" << channel2.name << "'"
              << std::endl;

    // Test 2: Try duplicate name (should fail)
    std::cout << "\n2. Testing duplicate name rejection:" << std::endl;
    try {
      auto &channel_dup = layout.AllocQueueChannel("consumer_a");
      std::cout << "   ERROR: Duplicate name should have been rejected!"
                << std::endl
                << channel_dup.name;
      return 1;
    } catch (const std::runtime_error &e) {
      std::cout << "   Correctly rejected duplicate: " << e.what() << std::endl;
    }

    // Test 3: Allocate third channel
    auto &channel3 = layout.AllocQueueChannel("consumer_c");
    std::cout << "   Channel 3 allocated: '" << channel3.name << "'"
              << std::endl;

    // Test 4: Try to exceed limit (should fail)
    std::cout << "\n3. Testing channel limit:" << std::endl;
    try {
      auto &channel_over = layout.AllocQueueChannel("consumer_d");
      std::cout << "   ERROR: Should not have exceeded channel limit! "
                << channel_over.name << " " << std::endl;
      return 1;
    } catch (const std::runtime_error &e) {
      std::cout << "   Correctly rejected over-limit allocation: " << e.what()
                << std::endl;
    }

    // Test 5: List allocated channels
    std::cout << "\n4. Listing allocated channels:" << std::endl;
    auto names = layout.getAllocatedChannelNames();
    for (const auto &name : names) {
      std::cout << "   - '" << name << "'" << std::endl;
    }

    // Verify we have exactly 3 names
    if (names.size() != 3) {
      std::cout << "   ERROR: Expected 3 allocated channels, got "
                << names.size() << std::endl;
      return 1;
    }

    // Test 6: Deallocate one channel
    std::cout << "\n5. Testing deallocation:" << std::endl;
    layout.deallocateChannel("consumer_b");
    std::cout << "   Deallocated 'consumer_b'" << std::endl;

    // List channels again
    names = layout.getAllocatedChannelNames();
    std::cout << "   Remaining channels:" << std::endl;
    for (const auto &name : names) {
      std::cout << "   - '" << name << "'" << std::endl;
    }

    if (names.size() != 2) {
      std::cout << "   ERROR: Expected 2 channels after deallocation, got "
                << names.size() << std::endl;
      return 1;
    }

    // Test 7: Try to deallocate non-existent channel
    std::cout << "\n6. Testing deallocation of non-existent channel:"
              << std::endl;
    try {
      layout.deallocateChannel("nonexistent");
      std::cout << "   ERROR: Should not have found non-existent channel!"
                << std::endl;
      return 1;
    } catch (const std::runtime_error &e) {
      std::cout << "   Correctly rejected non-existent channel: " << e.what()
                << std::endl;
    }

    // Test 8: Verify we can allocate the same name after deallocation
    std::cout << "\n7. Re-allocating previously deallocated name:" << std::endl;
    try {
      auto &channel_realloc = layout.AllocQueueChannel("consumer_b");
      std::cout << "   ERROR: Should not allow reallocation beyond limit! "
                << channel_realloc.name << std::endl;
      return 1;
    } catch (const std::runtime_error &e) {
      std::cout
          << "   Correctly prevented reallocation (channel limit reached): "
          << e.what() << std::endl;
    }

    std::cout << "\n=== All tests passed! ===" << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  }

  // Cleanup
  shm_unlink(shm_name.c_str());
  return 0;
}