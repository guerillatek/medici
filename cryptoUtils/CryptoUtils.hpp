
#pragma once

#include <expected>
#include <stdint.h>
#include <string>
#include <string_view>
#include <vector>
#include <memory>

namespace crypto_utils {

     std::string generateKey();
     std::string getSignatureBase64(const char *key, uint_fast8_t keySize, const std::string_view data, bool decodeKey = false);
     std::string getSignatureHEX64(const char *key, const uint_fast8_t keySize, const std::string_view data);
     uint32_t generateMaskingKey();
     void maskFrame(char *json, std::size_t sz);

     std::string base64Encode(std::string_view input);
     std::string base64Decode(std::string_view &input);
     std::string getHEX64Signature(const std::string& secretKey, const std::string& payload);
     std::string generateWebSocketAcceptKey(const std::string &secKey);

    void BIO_log(std::string_view data);
    
    struct Signer {
        // factory method
        static std::unique_ptr<Signer> create(std::string_view key);

        virtual bool isSessionSigner() const = 0;

        virtual ~Signer() = default;
        virtual std::string sign(std::string_view data) = 0;
    protected:
        std::string base64Signature(std::string_view data); 
        std::string hex64Signature(std::string_view data); 
        };

}// namespace utils
