#ifndef OP_PQCHECKSIG_H
#define OP_PQCHECKSIG_H

#include <vector>
#include <span>
#include <script/script.h>

using valtype = std::vector<unsigned char>;

bool VerifyPQSignature(const valtype& sig_no_hashtype,
                       const valtype& pubkey,
                       std::span<const unsigned char, 32> sighash32);

#endif
