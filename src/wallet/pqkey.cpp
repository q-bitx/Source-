#include "pqkey.h"
#define DILITHIUM_MODE 3

#include <random.h>
#include <cstring>
#include <util/strencodings.h>
#include "wallet/pqsign.h"

#ifdef K
#undef K
#endif
#ifdef N
#undef N
#endif

extern "C" {
    #include "crypto/dilithium/params.h"
    #include "crypto/dilithium/sign.h"
}

//extern "C" int randombytes(unsigned char *x, unsigned long long xlen) {
  //  GetRandBytes(std::span<uint8_t>(reinterpret_cast<uint8_t*>(x), static_cast<size_t>(xlen)));
  //  return 0;
//}

std::string PQPublicKey::ToHex() const {
    return HexStr(data);
}

std::string PQPrivateKey::ToHex() const {
    return HexStr(data);
}

bool PQ_GenerateKeypair(PQPublicKey& pub, PQPrivateKey& priv) {
    std::vector<uint8_t> pk(CRYPTO_PUBLICKEYBYTES);
    std::vector<uint8_t> sk(CRYPTO_SECRETKEYBYTES);
    if (crypto_sign_keypair(pk.data(), sk.data()) != 0) return false;

    pub = PQPublicKey(pk);
    priv = PQPrivateKey(sk);
    return true;
}

bool PQ_Sign(const PQPrivateKey& priv, const std::vector<uint8_t>& msg, std::vector<uint8_t>& sig) {
    sig.resize(CRYPTO_BYTES);
    size_t siglen = 0;

    const uint8_t* ctx = nullptr;
    size_t ctxlen = 0;

    return crypto_sign_signature(sig.data(), &siglen, msg.data(), msg.size(), ctx, ctxlen, priv.data.data()) == 0;
}

bool PQ_Verify(const PQPublicKey& pub, const std::vector<uint8_t>& msg, const std::vector<uint8_t>& sig) {
    const uint8_t* ctx = nullptr;
    size_t ctxlen = 0;

    return crypto_sign_verify(sig.data(), sig.size(), msg.data(), msg.size(), ctx, ctxlen, pub.data.data()) == 0;
}
