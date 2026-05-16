#include "dilithium.h"

#include <crypto/dilithium_wrapper.h>

#include <cstring>

extern "C" {
#include "crypto/dilithium/api.h"
#include "crypto/dilithium/sign.h"
}

#ifndef CRYPTO_PUBLICKEYBYTES
#define CRYPTO_PUBLICKEYBYTES DILITHIUM_PUBLICKEYBYTES
#endif
#ifndef CRYPTO_SECRETKEYBYTES
#define CRYPTO_SECRETKEYBYTES DILITHIUM_SECRETKEYBYTES
#endif
#ifndef CRYPTO_BYTES
#define CRYPTO_BYTES DILITHIUM_SIGNATUREBYTES
#endif


const size_t DILITHIUM_PUBLICKEYBYTES = CRYPTO_PUBLICKEYBYTES;
const size_t DILITHIUM_SECRETKEYBYTES = CRYPTO_SECRETKEYBYTES;
const size_t DILITHIUM_SIGNATUREBYTES = CRYPTO_BYTES;

static_assert(pqcrypto::dilithium::PUBKEY_BYTES == CRYPTO_PUBLICKEYBYTES);
static_assert(pqcrypto::dilithium::PRIVKEY_BYTES == CRYPTO_SECRETKEYBYTES);
static_assert(pqcrypto::dilithium::SIG_BYTES == CRYPTO_BYTES);

bool PQ_GenerateKeypair(valtype& pubkey, valtype& privkey) {
    pqcrypto::dilithium::PQPublicKey pub{};
    pqcrypto::dilithium::PQPrivateKey priv{};
    try {
        pqcrypto::dilithium::keygen(&pub, &priv);
    } catch (...) {
        return false;
    }
    pubkey.assign(pub.data, pub.data + pqcrypto::dilithium::PUBKEY_BYTES);
    privkey.assign(priv.data, priv.data + pqcrypto::dilithium::PRIVKEY_BYTES);
    return true;
}

bool PQ_Sign(valtype& signature, const valtype& message, const valtype& privkey) {
    signature.resize(DILITHIUM_SIGNATUREBYTES);
    size_t siglen = 0;
    return crypto_sign_signature(signature.data(), &siglen,
                                 message.data(), message.size(),
                                 nullptr, 0,  // context not used
                                 privkey.data()) == 0;
}

bool PQ_Verify(const valtype& signature, const valtype& message, const valtype& pubkey) {
    return crypto_sign_verify(signature.data(), signature.size(),
                              message.data(), message.size(),
                              nullptr, 0,
                              pubkey.data()) == 0;
}
