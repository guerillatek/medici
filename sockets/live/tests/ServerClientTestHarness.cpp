#include "ServerClientTestHarness.hpp"
#include "medici/http/writeBufferToTempFile.hpp"
#include "medici/sockets/live/testCredentials/sslTestCerts.hpp"

namespace medici::tests {

Expected ServerClientTestHarness::sslInitialization =
    crypto_utils::OpenSSLUtils::initSSL();

std::string ServerClientTestHarness::serverCertFilePath =
    http::writeBufferToTempFile(std::string_view{serverCertificate},
                                "serverCert", ".pem");

std::string ServerClientTestHarness::serverKeyFilePath =
    http::writeBufferToTempFile(std::string_view{serverKey}, "serverKey",
                                ".key");

} // namespace medici::tests