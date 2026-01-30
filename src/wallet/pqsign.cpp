#define DILITHIUM_MODE 3

#include <vector>
#include <cstdint>

//extern "C" int randombytes(unsigned char* x, unsigned long long xlen) {
  //  GetRandBytes(std::span<uint8_t>(reinterpret_cast<uint8_t*>(x), static_cast<size_t>(xlen)));
   // return 0;
//}
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

bool PQC_GenerateKeypair(std::vector<uint8_t>& pub, std::vector<uint8_t>& priv) {
    pub.resize(CRYPTO_PUBLICKEYBYTES);
    priv.resize(CRYPTO_SECRETKEYBYTES);
    return crypto_sign_keypair(pub.data(), priv.data()) == 0;
}

bool PQC_Sign(const std::vector<uint8_t>& msg, const std::vector<uint8_t>& priv, std::vector<uint8_t>& sig) {
    sig.resize(CRYPTO_BYTES);
    size_t siglen;
    return crypto_sign_signature(sig.data(), &siglen, msg.data(), msg.size(), nullptr, 0, priv.data()) == 0;
}

bool PQC_Verify(const std::vector<uint8_t>& msg, const std::vector<uint8_t>& sig, const std::vector<uint8_t>& pub) {
    return crypto_sign_verify(sig.data(), sig.size(), msg.data(), msg.size(), nullptr, 0, pub.data()) == 0;
}
