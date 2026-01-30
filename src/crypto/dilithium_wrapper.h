#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" {
#include "crypto/dilithium/api.h"
}

namespace pqcrypto::dilithium {

static constexpr size_t PUBKEY_BYTES  = pqcrystals_dilithium3_PUBLICKEYBYTES;
static constexpr size_t PRIVKEY_BYTES = pqcrystals_dilithium3_SECRETKEYBYTES;
static constexpr size_t SIG_BYTES     = pqcrystals_dilithium3_BYTES;

class PQPublicKey {
public:
    uint8_t data[PUBKEY_BYTES]{};
};

class PQPrivateKey {
public:
    uint8_t data[PRIVKEY_BYTES]{};
};

void keygen(PQPublicKey* pub, PQPrivateKey* priv);
bool sign(const PQPrivateKey* priv, const uint8_t* msg, size_t msg_len, std::vector<unsigned char>& sig);
bool verify(const PQPublicKey* pub, const uint8_t* msg, size_t msg_len, const std::vector<unsigned char>& sig);

} // namespace pqcrypto::dilithium
