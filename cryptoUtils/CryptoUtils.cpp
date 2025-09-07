
#include "CryptoUtils.hpp"

#include <iconv.h>
#include <iomanip>
#include <iostream>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <stdexcept>
#include <string>
#include <vector>

#include <format>
#include <source_location>

#include <boost/assert.hpp>

#include <string_view>


namespace crypto_utils{


std::string getErrorText() {
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) {
        return std::string{"Cannot obtain crypto library error text"};
    }

    ERR_print_errors(bio);

    BUF_MEM *buf_mem;
    BIO_get_mem_ptr(bio, &buf_mem);
    auto errorText = std::string(std::string_view(buf_mem->data,
                                                  buf_mem->data + buf_mem->length));
    BIO_free(bio);
    return errorText;
}

std::string base64Encode(std::string_view input) {
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);// Do not use newlines to flush buffer
    BIO_write(bio, input.data(), input.size());
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);
    BIO_set_close(bio, BIO_NOCLOSE);
    BIO_free_all(bio);
    std::string b64text{bufferPtr->data, bufferPtr->length};
    BUF_MEM_free(bufferPtr);

    return b64text;
}

std::string base64Decode(std::string_view input) {
    BIO *bio, *b64;
    char *buffer = (char *) malloc(input.length());
    if (buffer == nullptr) {
        throw std::runtime_error("Failed to allocate memory");
    }

    bio = BIO_new_mem_buf(input.data(), input.length());
    if (!bio) {
        free(buffer);
        throw std::runtime_error("Failed to create BIO");
    }

    b64 = BIO_new(BIO_f_base64());
    if (!b64) {
        BIO_free(bio);
        free(buffer);
        throw std::runtime_error("Failed to create BIO filter");
    }

    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);

    int decodedLength = BIO_read(bio, buffer, input.length());
    if (decodedLength < 0) {
        BIO_free_all(bio);
        free(buffer);
        throw std::runtime_error("Base64 decoding failed");
    }

    std::string output(buffer, decodedLength);

    // Cleanup
    BIO_free_all(bio);
    free(buffer);

    return output;
}

std::string getSignatureHEX64(const char *key, const uint_fast8_t keySize, const std::string_view data) {
    std::array<unsigned char, SHA256_DIGEST_LENGTH> hmacResult;
    unsigned hmacLength;
    HMAC(EVP_sha256(), key, keySize, const_cast<unsigned char *>(reinterpret_cast<const unsigned char *>(data.data())), std::size(data), hmacResult.data(), &hmacLength);

    std::array<char, SHA256_DIGEST_LENGTH * 2 + 1> hexstring;
    BOOST_ASSERT(hmacLength <= std::size(hexstring));
    for (int i = 0; i < hmacLength and i < std::size(hexstring); ++i) {
        snprintf(&(hexstring[i * 2]), 3, "%02x", hmacResult[i]);
    }

    const std::string signature{std::string_view(hexstring.data(), SHA256_DIGEST_LENGTH * 2)};
    return signature;
}

std::string getSignatureBase64(const char *key, const uint_fast8_t keySize, const std::string_view data, bool decodeKey) {
    std::array<unsigned char, SHA256_DIGEST_LENGTH> hmacResult;
    unsigned hmacLength;
    if (!decodeKey) {
        HMAC(EVP_sha256(), key, keySize, reinterpret_cast<const unsigned char *>(data.data()), std::size(data), hmacResult.data(), &hmacLength);
    } else {
        auto decodedKey = base64Decode(std::string_view{key, keySize});
        HMAC(EVP_sha256(), decodedKey.data(), decodedKey.size(), reinterpret_cast<const unsigned char *>(data.data()), std::size(data), hmacResult.data(), &hmacLength);
    }
    return base64Encode(std::string_view{reinterpret_cast<char *>(hmacResult.data()), hmacLength});
}

void maskFrame(char *json, std::size_t sz) {
    if (reinterpret_cast<const uint8_t &>(json[1]) == (126 | 0b1000'0000)) {
        const auto *const masking_key = json + 2 + 2;
        for (std::size_t i = 0; i < sz - 8; ++i) {
            json[i + 8] = json[i + 8] ^ masking_key[i % 4];
        }
    } else {
        const auto *const masking_key = json + 2;
        for (std::size_t i = 0; i < sz - 6; ++i) {
            json[i + 6] = (json[i + 6] ^ masking_key[i % 4]);
        }
    }
}

std::string generateKey() {
    std::array<unsigned char, 16> key;
    RAND_bytes(key.data(), std::size(key));

#ifdef FNXDEBUG
    //BIO_dump_fp(stdout, key.data(), std::size(key));
#endif

    BIO *const b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

    BIO *const bmem = BIO_new(BIO_s_mem());

    BIO *bio = BIO_push(b64, bmem);
    BIO_write(bio, key.data(), std::size(key));
    BIO_flush(bio);

    BUF_MEM *ptr;
    BIO_get_mem_ptr(bmem, &ptr);

#ifdef FNXDEBUG
    //BIO_dump_fp(stdout, ptr->data, ptr->length);
#endif

    const std::string encodedKey(ptr->data, ptr->length);
    BIO_free_all(bio);
    return encodedKey;
};

uint32_t generateMaskingKey() {
    return std::rand();
};

// Function to convert byte array to hex string
std::string bytesToHexString(const unsigned char *bytes, size_t length) {
    std::stringstream hexStream;
    for (size_t i = 0; i < length; ++i) {
        hexStream << std::hex << std::setw(2) << std::setfill('0') << (int) bytes[i];
    }
    return hexStream.str();
}


// Function to compute SHA-256 hex signature
std::string getHEX64Signature(const std::string &secretKey, const std::string &payload) {

    unsigned char hmacResult[EVP_MAX_MD_SIZE];
    unsigned int hmacLength=EVP_MAX_MD_SIZE;

    HMAC(EVP_sha256(), 
         secretKey.data(), 
         secretKey.length(),
         reinterpret_cast<const unsigned char*>(payload.data()), 
         payload.length(), 
         hmacResult, 
         &hmacLength);

    // Convert the result to a hex string
    return bytesToHexString(hmacResult, hmacLength);
}


std::string generateWebSocketAcceptKey(const std::string &secKey) {
    const std::string magicString = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combinedKey = secKey + magicString; 
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char *>(combinedKey.data()), combinedKey.size(), hash);        
    return base64Encode(std::string_view(reinterpret_cast<char *>(hash), SHA_DIGEST_LENGTH));
}   

namespace {
    struct SHA256Signer : public Signer {
        SHA256Signer(std::string_view key) : key(key) {
            // check that key is sha256
            // if (key.size() != SHA256_DIGEST_LENGTH) {
            //     throw std::runtime_error("Invalid key size");
            // }
            // try to load key
            unsigned char test[SHA256_DIGEST_LENGTH];
            if (nullptr == HMAC(EVP_sha256(), key.data(), key.size(), reinterpret_cast<const unsigned char *>(key.data()), key.size(), test, nullptr)) {
                throw std::runtime_error("Invalid key");
            };
        }

        bool isSessionSigner() const override {
            return false;
        }

        std::string sign(std::string_view data) override {
            std::vector<unsigned char> signature(SHA256_DIGEST_LENGTH);
            unsigned hmacLength;
            HMAC(EVP_sha256(), key.data(), key.size(), reinterpret_cast<const unsigned char *>(data.data()), data.size(), signature.data(), &hmacLength);
            signature.resize(hmacLength);
            BOOST_ASSERT(hmacLength == SHA256_DIGEST_LENGTH);

            return bytesToHexString(signature.data(), signature.size());
        }

    private:
        std::string key;
    };

    struct SSLSigner : public Signer {
        EVP_MD_CTX *mdctx;
        EVP_PKEY *pkey = nullptr;
        BIO *bio = nullptr;
        bool isSessionSigner() const override {
            return true;
        }

        SSLSigner(std::string_view pem_key) {
            std::string pem_key_str;
            if (pem_key.find("-----BEGIN") == std::string::npos) {
                // create pem key from secret key
                pem_key_str = "-----BEGIN PRIVATE KEY-----\n" + std::string(pem_key) + "\n-----END PRIVATE KEY-----";// nosemgrep
                pem_key = pem_key_str;
            }

            BIO *bio = BIO_new_mem_buf(pem_key.data(), pem_key.size());
            if (!bio) {
                std::cerr << "Failed to create BIO object." << std::endl;
                throw std::runtime_error("Failed to create BIO object.");
            }

            // Load the private key
            pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
            if (!pkey) {
                BIO_free(bio);
                //_LOG_DEBUG(std::format("Failed to load private key. {}", pem_key));
                throw std::runtime_error("Failed to load private key.");
            }

            mdctx = EVP_MD_CTX_new();
            if (!mdctx) {
                throw std::runtime_error("Failed to create EVP_MD_CTX.");
            }
        }
        ~SSLSigner() {
            EVP_MD_CTX_free(mdctx);
            if (pkey) {
                EVP_PKEY_free(pkey);
            }
            if (bio) {
                BIO_free(bio);
            }
        }

    private:
        std::string sign(std::string_view data) override {
            std::vector<unsigned char> signature(64 + 8);

            if (EVP_DigestSignInit(mdctx, NULL, NULL, NULL, pkey) != 1) {
                throw std::runtime_error("Failed to initialize DigestSign.");
            }
            size_t sig_len = 0;
            if (EVP_DigestSign(mdctx, NULL, &sig_len, reinterpret_cast<const unsigned char *>(data.data()), data.size()) != 1) {
                throw std::runtime_error("Failed to sign data.");
            }
            signature.resize(sig_len);
            if (EVP_DigestSign(mdctx, signature.data(), &sig_len, reinterpret_cast<const unsigned char *>(data.data()), data.size()) != 1) {
                throw std::runtime_error("Failed to sign data.");
            }
            signature.resize(sig_len);
            return base64Encode(std::string_view{reinterpret_cast<char *>(signature.data()), signature.size()});
        }
    };
}// namespace

std::unique_ptr<Signer> Signer::create(std::string_view key) {

    try {
        return std::make_unique<SSLSigner>(key);
    } catch (const std::exception &e) {
        return std::make_unique<SHA256Signer>(key);
    }
}

void BIO_log(std::string_view data) {

#ifdef FNXDEBUG
   BIO* bio = BIO_new(BIO_s_mem()); // Create memory BIO
   if (!bio) {
       ERR_print_errors_fp(stderr); // Log OpenSSL errors
       return;
   }

   // Write the hex/ASCII dump to the memory BIO
   BIO_dump(bio, "\n", 1); // write a newline first to make the hex line flush to the next line
   BIO_dump(bio, data.data(), data.size());

   // Extract data from the memory BIO
   char* dump_buffer = NULL;
   long dump_len = BIO_get_mem_data(bio, &dump_buffer); // Get pointer and length

   if (dump_len <= 0) {
       BIO_free(bio);
       return;
   }

   LOG_INFO(std::string_view(dump_buffer, dump_len));

   BIO_free(bio); // Free the memory BIO
#endif
}
}