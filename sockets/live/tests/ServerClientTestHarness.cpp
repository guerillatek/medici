#include "ServerClientTestHarness.hpp"
#include "sslTestCerts.hpp"

namespace medici::tests {

Expected ServerClientTestHarness::sslInitialization =
    crypto_utils::OpenSSLUtils::initSSL();

std::string ServerClientTestHarness::serverCertFilePath =
    writeStringToTempFile(serverCertificate, "serverCert", ".pem");

std::string ServerClientTestHarness::serverKeyFilePath =
    writeStringToTempFile(serverKey, "serverKey", ".key");

} // namespace medici::tests