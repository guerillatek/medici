
#include "OpenSSLUtils.hpp"

#include <openssl/err.h>

#include <expected>
#include <format>

namespace crypto_utils {

SSL_CTX *OpenSSLUtils::ctxP = nullptr;

Expected OpenSSLUtils::initSSL() {

  if (1 != SSL_load_error_strings()) {
    return std::unexpected(
        std::format("<< EXIT_FAILURE. FAIL ON SSL_load_error_strings()"));
  }
  if (1 != OpenSSL_add_all_algorithms()) {
    return std::unexpected(
        std::format("<< EXIT_FAILURE. FAIL ON OpenSSL_add_all_algorithms()"));
  }
  if (1 != SSL_load_error_strings()) {
    return std::unexpected(
        std::format("<< EXIT_FAILURE. FAIL ON SSL_load_error_strings()"));
  }
  if (1 != ERR_load_crypto_strings()) {
    return std::unexpected(
        std::format("<< EXIT_FAILURE. FAIL ON ERR_load_crypto_strings()"));
  }
  if (1 != SSL_library_init()) {
    return std::unexpected(
        std::format("<< EXIT_FAILURE. FAIL ON SSL_library_init()"));
  }

  return {};
}

Expected OpenSSLUtils::sendOpenSSL(ssl_st *sslP, std::string_view json,
                                   const std::source_location &sourceLocation) {
  if (not sslP) {
    return std::unexpected(std::format(
        "{} : << ERROR. sslP IS NULL [{}:{}] {}",
        std::source_location::current().function_name(), std::size(json),
        ::strrchr(sourceLocation.file_name(), '/') + 1, sourceLocation.line(),
        sourceLocation.function_name()));
  }

  for (std::size_t offset = 0;;) {
    const auto n =
        SSL_write(sslP, json.data() + offset, std::size(json) - offset);
    if (n < 0) [[unlikely]] {
      int ssl_err = SSL_get_error(sslP, n);
      int err = errno;
      return std::unexpected(std::format(
          "{} : << ERROR. ec={} ssl_err={} errno={} size={} [{}:{}] {}",
          std::source_location::current().function_name(), n, ssl_err, err,
          std::size(json), ::strrchr(sourceLocation.file_name(), '/') + 1,
          sourceLocation.line(), sourceLocation.function_name()));
    }
    offset += n;
    if (offset < std::size(json))
      continue;

    return {};
  }
}

} // namespace crypto_utils
