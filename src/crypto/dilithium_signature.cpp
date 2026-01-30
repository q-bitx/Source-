#include "dilithium_signature.h"
#include <oqs/oqs.h>
#include <cstring>
#include <iostream>

DilithiumSignature::DilithiumSignature() {}
DilithiumSignature::~DilithiumSignature() {}

bool DilithiumSignature::GenerateKeypair(std::vector<uint8_t>& pubkey, std::vector<uint8_t>& privkey)
{
    OQS_SIG *sig = OQS_SIG_new("Dilithium3");
    if (!sig) return false;

    pubkey.resize(sig->length_public_key);
    privkey.resize(sig->length_secret_key);

    OQS_STATUS result = OQS_SIG_keypair(sig, pubkey.data(), privkey.data());

    OQS_SIG_free(sig);
    return result == OQS_SUCCESS;
}

bool DilithiumSignature::Sign(const std::vector<uint8_t>& msg, const std::vector<uint8_t>& privkey, std::vector<uint8_t>& signature)
{
    OQS_SIG *sig = OQS_SIG_new("Dilithium3");
    if (!sig) return false;

    signature.resize(sig->length_signature);
    size_t sig_len = 0;

    OQS_STATUS result = OQS_SIG_sign(sig, signature.data(), &sig_len, msg.data(), msg.size(), privkey.data());

    OQS_SIG_free(sig);
    return result == OQS_SUCCESS;
}

bool DilithiumSignature::Verify(const std::vector<uint8_t>& msg, const std::vector<uint8_t>& pubkey, const std::vector<uint8_t>& signature)
{
    OQS_SIG *sig = OQS_SIG_new("Dilithium3");
    if (!sig) return false;

    OQS_STATUS result = OQS_SIG_verify(sig, msg.data(), msg.size(), signature.data(), signature.size(), pubkey.data());

    OQS_SIG_free(sig);
    return result == OQS_SUCCESS;
}

