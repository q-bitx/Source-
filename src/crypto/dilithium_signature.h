#ifndef QBITX_DILITHIUM_SIGNATURE_H
#define QBITX_DILITHIUM_SIGNATURE_H

#include <vector>
#include <cstdint>
#include <string>

static constexpr size_t DILITHIUM_PUBLIC_KEY_SIZE = 1312;
static constexpr size_t DILITHIUM_PRIVATE_KEY_SIZE = 2528;
static constexpr size_t DILITHIUM_SIGNATURE_SIZE = 2420;

class DilithiumSignature
{
public:
    DilithiumSignature();
    ~DilithiumSignature();

    bool GenerateKeypair(std::vector<uint8_t>& pubkey, std::vector<uint8_t>& privkey);
    bool Sign(const std::vector<uint8_t>& msg, const std::vector<uint8_t>& privkey, std::vector<uint8_t>& signature);
    bool Verify(const std::vector<uint8_t>& msg, const std::vector<uint8_t>& pubkey, const std::vector<uint8_t>& signature);
};

#endif // QBITX_DILITHIUM_SIGNATURE_H
