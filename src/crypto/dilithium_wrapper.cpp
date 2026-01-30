#include "crypto/dilithium_wrapper.h"

#include <cstring>
#include <stdexcept>

extern "C" {
#include "crypto/dilithium/api.h"
}

extern "C" {
int pqcrystals_dilithium3_ref_keypair(uint8_t *pk, uint8_t *sk);

int pqcrystals_dilithium3_ref_signature(uint8_t *sig, size_t *siglen,
                                        const uint8_t *m, size_t mlen,
                                        const uint8_t *ctx, size_t ctxlen,
                                        const uint8_t *sk);

int pqcrystals_dilithium3_ref_verify(const uint8_t *sig, size_t siglen,
                                     const uint8_t *m, size_t mlen,
                                     const uint8_t *ctx, size_t ctxlen,
                                     const uint8_t *pk);
}

namespace pqcrypto::dilithium {

static_assert(PUBKEY_BYTES  == pqcrystals_dilithium3_PUBLICKEYBYTES,  "PUBKEY_BYTES mismatch (Dilithium3)");
static_assert(PRIVKEY_BYTES == pqcrystals_dilithium3_SECRETKEYBYTES,  "PRIVKEY_BYTES mismatch (Dilithium3)");
static_assert(SIG_BYTES     == pqcrystals_dilithium3_BYTES,           "SIG_BYTES mismatch (Dilithium3)");

void keygen(PQPublicKey* pub, PQPrivateKey* priv)
{
    if (!pub || !priv) return;
    int ret = pqcrystals_dilithium3_ref_keypair(pub->data, priv->data);
    if (ret != 0) {
        throw std::runtime_error("pqcrystals_dilithium3_ref_keypair failed");
    }
}

bool sign(const PQPrivateKey* priv, const uint8_t* msg, size_t msg_len,
          std::vector<unsigned char>& sig)
{
    if (!priv || (!msg && msg_len != 0)) return false;

    sig.resize(SIG_BYTES);
    size_t siglen = 0;

    int ret = pqcrystals_dilithium3_ref_signature(sig.data(), &siglen,
                                                  msg, msg_len,
                                                  nullptr, 0,
                                                  priv->data);
    if (ret != 0) return false;

    sig.resize(siglen);
    return true;
}

bool verify(const PQPublicKey* pub, const uint8_t* msg, size_t msg_len,
            const std::vector<unsigned char>& sig)
{
    if (!pub || (!msg && msg_len != 0)) return false;
    if (sig.empty()) return false;

    int ret = pqcrystals_dilithium3_ref_verify(sig.data(), sig.size(),
                                               msg, msg_len,
                                               nullptr, 0,
                                               pub->data);
    return ret == 0;
}

} // namespace pqcrypto::dilithium
