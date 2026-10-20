// Copyright (c) 2026-present The Q-BitX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_PQ_SPEND_WEIGHT_H
#define BITCOIN_WALLET_PQ_SPEND_WEIGHT_H

#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <crypto/dilithium.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <serialize.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wallet {

/** Same non-final sequence pqsendtoaddress / pqsendfrom put on every input. */
static constexpr uint32_t PQ_SPEND_SEQUENCE{0xFFFFFFFE};

/**
 * Canonical weight of a Dilithium satisfaction, measured by
 * GetTransactionWeightWithScale / GetVirtualTransactionSize.
 *
 * The skeleton uses the same version, locktime, sequence, push encoding, and
 * witness stack layout as the PQ send RPCs. Signature and pubkey bytes are
 * the real Dilithium3 sizes, so the estimate is the signed transaction's
 * weight when those sizes match, not the weight of an empty unsigned input.
 *
 * witness_size is total_size - stripped_size. For a witness transaction that
 * includes the BIP141 marker and flag. For a legacy scriptSig transaction it
 * is zero.
 */
struct PqSpendWeight {
    int32_t stripped_size{0};
    int32_t total_size{0};
    int32_t witness_size{0};
    int32_t weight{0};
    int64_t vsize{0};
};

inline std::vector<unsigned char> PqSatisfactionSignature()
{
    return std::vector<unsigned char>(DILITHIUM_SIGNATUREBYTES + 1, 0);
}

inline std::vector<unsigned char> PqSatisfactionPubkey()
{
    return std::vector<unsigned char>(DILITHIUM_PUBLICKEYBYTES, 0);
}

inline CMutableTransaction BuildPqSatisfactionTransaction(size_t num_inputs,
                                                          bool legacy_scriptsig,
                                                          const std::vector<CScript>& output_scripts)
{
    CMutableTransaction mtx;
    mtx.vin.resize(num_inputs);
    for (size_t i = 0; i < num_inputs; ++i) {
        mtx.vin[i].nSequence = PQ_SPEND_SEQUENCE;
        if (legacy_scriptsig) {
            CScript script_sig;
            script_sig << PqSatisfactionSignature() << PqSatisfactionPubkey();
            mtx.vin[i].scriptSig = std::move(script_sig);
        } else {
            mtx.vin[i].scriptSig.clear();
            mtx.vin[i].scriptWitness.stack = {PqSatisfactionSignature(), PqSatisfactionPubkey()};
        }
    }
    mtx.vout.reserve(output_scripts.size());
    for (const CScript& script : output_scripts) {
        mtx.vout.emplace_back(CAmount{0}, script);
    }
    return mtx;
}

inline PqSpendWeight MeasurePqSpendWeight(const CTransaction& tx, int witness_scale)
{
    PqSpendWeight out;
    out.stripped_size = static_cast<int32_t>(::GetSerializeSize(TX_NO_WITNESS(tx)));
    out.total_size = static_cast<int32_t>(::GetSerializeSize(TX_WITH_WITNESS(tx)));
    out.witness_size = out.total_size - out.stripped_size;
    out.weight = GetTransactionWeightWithScale(tx, witness_scale);
    out.vsize = GetVirtualTransactionSize(out.weight, /*nSigOpCost=*/0, /*bytes_per_sigop=*/0, witness_scale);
    return out;
}

inline PqSpendWeight EstimatePqSpendWeight(size_t num_inputs,
                                           bool legacy_scriptsig,
                                           int witness_scale,
                                           const std::vector<CScript>& output_scripts)
{
    const CTransaction skeleton{BuildPqSatisfactionTransaction(num_inputs, legacy_scriptsig, output_scripts)};
    return MeasurePqSpendWeight(skeleton, witness_scale);
}

inline bool PqSpendExceedsStandardWeight(size_t num_inputs,
                                         bool legacy_scriptsig,
                                         int witness_scale,
                                         const std::vector<CScript>& output_scripts)
{
    if (num_inputs == 0 || output_scripts.empty()) return false;
    return EstimatePqSpendWeight(num_inputs, legacy_scriptsig, witness_scale, output_scripts).weight > MAX_STANDARD_TX_WEIGHT;
}

} // namespace wallet

#endif // BITCOIN_WALLET_PQ_SPEND_WEIGHT_H
