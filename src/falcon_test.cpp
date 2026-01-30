#include "../crypto/falcon/api.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

int main() {
    printf("=== Falcon test ===\n");

    uint8_t pk[1024];
    uint8_t sk[1024];
    uint8_t sig[1024];
    size_t siglen;

    const uint8_t message[] = "Hello from Q-BitX!";
    size_t message_len = strlen((const char*)message);

    if (crypto_sign_keypair(pk, sk) != 0) {
        printf("Falcon: Keypair generation failed.\n");
        return 1;
    }

    if (crypto_sign_signature(sig, &siglen, message, message_len, sk) != 0) {
        printf("Falcon: Signing failed.\n");
        return 1;
    }

    if (crypto_sign_verify(sig, siglen, message, message_len, pk) != 0) {
        printf("Falcon: Verification failed.\n");
        return 1;
    }

    printf("Falcon: Signature verified successfully!\n");
    return 0;
}
