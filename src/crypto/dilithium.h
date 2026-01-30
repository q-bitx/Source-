// dilithium.h
#pragma once

#include <vector>
#include <cstddef>

using valtype = std::vector<unsigned char>;

extern const size_t DILITHIUM_PUBLICKEYBYTES;
extern const size_t DILITHIUM_SECRETKEYBYTES;
extern const size_t DILITHIUM_SIGNATUREBYTES;

bool PQ_GenerateKeypair(valtype& pubkey, valtype& privkey);
bool PQ_Sign(valtype& signature, const valtype& message, const valtype& privkey);
bool PQ_Verify(const valtype& signature, const valtype& message, const valtype& pubkey);
