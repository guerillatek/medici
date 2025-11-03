#include "medici/shm_endpoints/testUtils.hpp"
#include <concepts>

int main(int argc, char *argv[]) {
  std::cout << "Starting consumer process with ID: " << argv[1] << std::endl;
  medici::tests::runClientProducerFunction(argv[1]);
  return 0;
}