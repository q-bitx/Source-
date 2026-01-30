#ifndef QBITX_CRYPTO_PQKEY_H
#define QBITX_CRYPTO_PQKEY_H

#include <cstdint>
#include <utility>

#include "serialize.h"
#include <vector>
#include <string>

class PQPublicKey {
public:
    std::vector<uint8_t> data;

    PQPublicKey() = default;
    explicit PQPublicKey(std::vector<uint8_t> d) : data(std::move(d)) {}

    std::string ToHex() const;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(data);
    }
};

class PQPrivateKey {
public:
    std::vector<uint8_t> data;

    PQPrivateKey() = default;
    explicit PQPrivateKey(std::vector<uint8_t> d) : data(std::move(d)) {}

    std::string ToHex() const;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(data);
    }
};

bool PQ_GenerateKeypair(PQPublicKey& pub, PQPrivateKey& priv);
bool PQ_Sign(const PQPrivateKey& priv, const std::vector<uint8_t>& msg, std::vector<uint8_t>& sig);
bool PQ_Verify(const PQPublicKey& pub, const std::vector<uint8_t>& msg, const std::vector<uint8_t>& sig);

#endif // QBITX_CRYPTO_PQKEY_H
