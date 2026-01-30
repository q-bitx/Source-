//#include <iostream>
//extern "C" {
//#include "crypto/dilithium/sign.h"
//}
//int main() {
  //  uint8_t pk[CRYPTO_PUBLICKEYBYTES];
    //uint8_t sk[CRYPTO_SECRETKEYBYTES];

    //int result = crypto_sign_keypair(pk, sk);
    //if (result == 0) {
      //  std::cout << "Q-BitX: Dilithium keypair generated successfully." << std::endl;
    //} else {
      //  std::cerr << "Q-BitX: Failed to generate Dilithium keypair!" << std::endl;
   // }

   // return 0;
//
#include <cstdlib>
#include <iostream>
#include "uint256.h"
#include "init.h"
#include "util/fs.h"
#include "util/system.h" //posle
#include "util/strencodings.h"
#include "util/threadnames.h"
#include "util/translation.h"
#include "common/system.h" //posle
void SetupTranslations() {
    std::cout << "SetupTranslations - translation init (stub)..." << std::endl;
}

bool AppInit(int argc, char* argv[]) {
    std::cout << "AppInit - initialization..." << std::endl;
    return true;
}
int main(int argc, char* argv[])
{
    SetupEnvironment();

    // Initialize translations
    SetupTranslations();

    try {
        if (!AppInit(argc, argv)) {
            return EXIT_FAILURE;
        }
    } catch (const std::exception& e) {
        PrintExceptionContinue(&e, "AppInit()");
        return EXIT_FAILURE;
    } catch (...) {
        PrintExceptionContinue(nullptr, "AppInit()");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
