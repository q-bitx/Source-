#include <iostream>
#include <iomanip>
#include <random>
#include <cstring>
extern "C" {
#include "../crypto/dilithium/sign.h"
#include "../crypto/dilithium/params.h"
#include "../crypto/dilithium/api.h"
}

#include <iostream>
#include <cstring>
#include <stdint.h>

void print_hex(const char* label, const uint8_t* data, size_t len) {
    std::cout << label << ": ";
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
    }
    std::cout << std::endl;
}

extern "C" {
    void randombytes(uint8_t* buf, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            buf[i] = rand() % 256;
        }
    }
}

int main() {
    uint8_t pk[CRYPTO_PUBLICKEYBYTES];
    uint8_t sk[CRYPTO_SECRETKEYBYTES];

if (crypto_sign_keypair(pk, sk) != 0) {
    std::cerr << "Keypair generation failed!" << std::endl;
    return 1;
}

std::cout << "Keypair generated successfully!" << std::endl;

std::cout << "Public Key: ";
for (int i = 0; i < CRYPTO_PUBLICKEYBYTES; i++) {
    std::cout << std::hex << (int)pk[i] << " ";
}
std::cout << std::endl;

std::cout << "Secret Key: ";
for (int i = 0; i < CRYPTO_SECRETKEYBYTES; i++) {
    std::cout << std::hex << (int)sk[i] << " ";
}
std::cout << std::endl;

const char *message = "This is a long message for Hello, Dilithium!";
size_t msglen = std::strlen(message);

uint8_t sig[CRYPTO_BYTES];
size_t siglen = 0;

    uint8_t ctx[1] = {0};
    size_t ctxlen = 0;


    if (pqcrystals_dilithium2_ref(
            sig, &siglen,
            (const uint8_t*)message, msglen,
            sk, CRYPTO_SECRETKEYBYTES,
            nullptr) != 0) {
        std::cerr << "Signing failed!" << std::endl;
        return 1;
}
std::cout << "Signature: ";
for (size_t i = 0; i < siglen; i++) {
    std::cout << std::hex << (int)sig[i] << " ";
}
std::cout << std::endl;

    if (pqcrystals_dilithium2_ref_verify(
            sig, siglen,
            (const uint8_t*)message, msglen,
            pk, CRYPTO_PUBLICKEYBYTES,
            nullptr) != 0) {
        std::cerr << "Signature verification failed!" << std::endl;
    } else {
        std::cout << "Signature verification succeeded!" << std::endl;
    }

    print_hex("Message", reinterpret_cast<const uint8_t *>(message), msglen);
    print_hex("Signature", sig, siglen);
    print_hex("Public key", pk, CRYPTO_PUBLICKEYBYTES);

    if (crypto_sign_verify(sig, siglen, reinterpret_cast<const uint8_t *>(message), msglen, ctx, ctxlen, pk) != 0) {
        std::cerr << "Signature verification failed!" << std::endl;
        return 1;
    }

    std::cout << "Signature verified successfully!" << std::endl;
    return 0;
}
