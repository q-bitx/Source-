#include "script/op_pqchecksig.h"
#include "crypto/dilithium.h"

bool VerifyPQSignature(const valtype& sig_no_hashtype,
                       const valtype& pubkey,
                       std::span<const unsigned char, 32> sighash32)
{
    if (pubkey.size() != DILITHIUM_PUBLICKEYBYTES) return false;
    if (sig_no_hashtype.size() != DILITHIUM_SIGNATUREBYTES) return false;

    valtype msg(sighash32.begin(), sighash32.end());
    return PQ_Verify(sig_no_hashtype, msg, pubkey);
}
