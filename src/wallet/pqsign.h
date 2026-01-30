#ifndef QBITX_CRYPTO_PQSIGN_H
#define QBITX_CRYPTO_PQSIGN_H

#include <vector>
#include <cstdint>

bool PQC_GenerateKeypair(std::vector<uint8_t>& pub, std::vector<uint8_t>& priv);
bool PQC_Sign(const std::vector<uint8_t>& msg, const std::vector<uint8_t>& priv, std::vector<uint8_t>& sig);
bool PQC_Verify(const std::vector<uint8_t>& msg, const std::vector<uint8_t>& sig, const std::vector<uint8_t>& pub);

#endif // QBITX_CRYPTO_PQSIGN_H
