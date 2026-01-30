
int crypto_sign_keypair(uint8_t *pk, uint8_t *sk) {
    return pqcrystals_dilithium3_ref_keypair(pk, sk);
}

int crypto_sign(uint8_t *sig, size_t *siglen,
                const uint8_t *m, size_t mlen,
                const uint8_t *sk) {
    return pqcrystals_dilithium3_ref_signature(sig, siglen, m, mlen, NULL, 0, sk);
}

int crypto_sign_open(uint8_t *m, size_t *mlen,
                     const uint8_t *sig, size_t siglen,
                     const uint8_t *pk) {
    return pqcrystals_dilithium3_ref_verify(sig, siglen, m, *mlen, NULL, 0, pk);
}
