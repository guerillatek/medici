
#pragma once

#include <openssl/ssl.h>
#include <source_location>
#include <string_view>
#include <expected>

namespace crypto_utils {
using Expected = std::expected<void, std::string>;

struct OpenSSLUtils {
  static SSL_CTX *ctxP;
  static Expected initSSL();

  static Expected sendOpenSSL(ssl_st *sslP, std::string_view json,
                              const std::source_location &sl);
};

} // namespace crypto_utils
