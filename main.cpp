#include <iostream>
extern "C" {
  #include "crypto/dilithium/ref/api.h"
}
int main() {
    uint8_t pk[CRYPTO_PUBLICKEYBYTES];
    uint8_t sk[CRYPTO_SECRETKEYBYTES];

    int result = crypto_sign_keypair(pk, sk);
    if (result == 0) {
        std::cout << "Q-BitX: Dilithium keypair generated successfully." << std::endl;
    } else {
        std::cerr << "Q-BitX: Failed to generate Dilithium keypair!" << std::endl;
    }

    return 0;
}
