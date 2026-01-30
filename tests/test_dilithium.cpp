#include <iostream>
#include <cstring>
#include <stdint.h>

extern "C" {
    #include "../src/crypto/dilithium/api.h"
    #include "../src/crypto/dilithium/randombytes.h"
}

void print_hex(const char *label, const uint8_t *data, size_t len) {
    std::cout << label << ": ";
    for (size_t i = 0; i < len; ++i)
        printf("%02x", data[i]);
    std::cout << std::endl;
}

int main() {
    uint8_t pk[CRYPTO_PUBLICKEYBYTES];
    uint8_t sk[CRYPTO_SECRETKEYBYTES];
    uint8_t sig[CRYPTO_BYTES];
    size_t siglen;

    const uint8_t ctx[] = {};
    size_t ctxlen = 0;

    const char *message = "Hello, Dilithium!";
    size_t mlen = std::strlen(message);
    const uint8_t *msg = reinterpret_cast<const uint8_t *>(message);

    if (crypto_sign_keypair(pk, sk) != 0) {
        std::cerr << "Key generation failed!" << std::endl;
        return 1;
    }

    std::cout << "Keypair generated!" << std::endl;

    if (crypto_sign(sig, &siglen, msg, mlen, ctx, ctxlen, sk) != 0) {
        std::cerr << "Signing failed!" << std::endl;
        return 1;
    }

    std::cout << "Message signed!" << std::endl;

    print_hex("Message", msg, mlen);
    print_hex("Signature", sig, siglen);
    print_hex("Public Key", pk, CRYPTO_PUBLICKEYBYTES);

    if (crypto_sign_verify(sig, siglen, msg, mlen, ctx, ctxlen, pk) != 0) {
        std::cerr << "Signature verification failed!" << std::endl;
        return 1;
    }

    std::cout << "Signature verified successfully!" << std::endl;
    return 0;
}
