// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util/check.h>
#include <logging.h>

#include <algorithm>

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{

    // Temporary debug logging for EDA behavior (do NOT use MTP in the delta computation).
    // Note: MTP may be useful for context in debugging, but must never affect EDA triggering.
    const uint32_t last_time = pindexLast ? (uint32_t)pindexLast->GetBlockTime() : 0;
    const uint32_t cand_time = pblock ? (uint32_t)pblock->GetBlockTime() : 0;
    const int64_t delta = (pblock && pindexLast) ? (int64_t)cand_time - (int64_t)last_time : 0;
    LogPrintf("[POW][EDA] dbg height=%d next=%d window=%u enabled=%d allowMin=%d retargetBoundary=%d last_time=%u cand_time=%u delta=%d last_bits=%08x mtp=%u\n",
              pindexLast ? pindexLast->nHeight : -1,
              pindexLast ? pindexLast->nHeight + 1 : -1,
              (unsigned int)params.nPowEmergencyWindow,
              params.fPowEnableEmergencyDifficultyDrop ? 1 : 0,
              params.fPowAllowMinDifficultyBlocks ? 1 : 0,
              (pindexLast && ((pindexLast->nHeight + 1) % params.DifficultyAdjustmentInterval() == 0)) ? 1 : 0,
              last_time,
              cand_time,
              (int)delta,
              pindexLast ? pindexLast->nBits : 0,
              pindexLast ? (uint32_t)pindexLast->GetMedianTimePast() : 0);
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        // Emergency Difficulty Adjustment (EDA): Litecoin-style emergency difficulty drop
        // Only applies when NOT at a retarget boundary and EDA is enabled
        // EDA is disabled after height 200000
        if (params.fPowEnableEmergencyDifficultyDrop && (pindexLast->nHeight + 1 < 200000)) {
            // Trigger EDA using *pure block header times* (no MTP in delta decision).
            // This avoids blocking EDA when MTP lags ahead of header times.
            if (pblock != nullptr) {
                const int64_t prev_time = pindexLast->GetBlockTime();
                const int64_t cand_time = pblock->GetBlockTime();
                const int64_t time_delta = cand_time - prev_time;
                if (time_delta >= params.nPowEmergencyWindow) {
                arith_uint256 bnOld;
                bnOld.SetCompact(pindexLast->nBits);
                arith_uint256 bnNew = bnOld;
                bnNew <<= 1; // Double the target (halve difficulty)
                const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
                if (bnNew > bnPowLimit) {
                    bnNew = bnPowLimit;
                }
                unsigned int nNewBits = bnNew.GetCompact();
                LogPrintf("[POW][EDA] trigger height=%d last_time=%u cand_time=%u delta=%d window=%u old_bits=%08x new_bits=%08x\n",
                         pindexLast->nHeight + 1,
                         (unsigned int)prev_time,
                         (unsigned int)cand_time,
                         (int)time_delta,
                         (unsigned int)params.nPowEmergencyWindow,
                         pindexLast->nBits,
                         nNewBits);
                return nNewBits;
                }
            }
        }

        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then it MUST be a min-difficulty block.
            if (pblock && pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/100)
        nActualTimespan = params.nPowTargetTimespan/100;
    if (nActualTimespan > params.nPowTargetTimespan*100)
        nActualTimespan = params.nPowTargetTimespan*100;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;

    // Special difficulty rule for Testnet4
    if (params.enforce_BIP94) {
        // Here we use the first block of the difficulty period. This way
        // the real difficulty is always preserved in the first block as
        // it is not allowed to use the min-difficulty exception.
        int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
        const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
        bnNew.SetCompact(pindexFirst->nBits);
    } else {
        bnNew.SetCompact(pindexLast->nBits);
    }

    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    arith_uint256 old_target;
old_target.SetCompact(pindexLast->nBits);

arith_uint256 min_target = old_target;
min_target /= 100;

arith_uint256 max_target = old_target;
max_target *= 100;

if (bnNew < min_target) bnNew = min_target;
if (bnNew > max_target) bnNew = max_target;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

// Check that on difficulty adjustments, the new difficulty does not increase
// or decrease beyond the permitted limits.
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits)
{
    if (params.fPowAllowMinDifficultyBlocks) return true;

    if (height % params.DifficultyAdjustmentInterval() == 0) {
        int64_t smallest_timespan = params.nPowTargetTimespan/4;
        int64_t largest_timespan = params.nPowTargetTimespan*4;

        const arith_uint256 pow_limit = UintToArith256(params.powLimit);
        arith_uint256 observed_new_target;
        observed_new_target.SetCompact(new_nbits);

        // Calculate the largest difficulty value possible:
        arith_uint256 largest_difficulty_target;
        largest_difficulty_target.SetCompact(old_nbits);
        largest_difficulty_target *= largest_timespan;
        largest_difficulty_target /= params.nPowTargetTimespan;

        if (largest_difficulty_target > pow_limit) {
            largest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 maximum_new_target;
        maximum_new_target.SetCompact(largest_difficulty_target.GetCompact());
        if (maximum_new_target < observed_new_target) return false;

        // Calculate the smallest difficulty value possible:
        arith_uint256 smallest_difficulty_target;
        smallest_difficulty_target.SetCompact(old_nbits);
        smallest_difficulty_target *= smallest_timespan;
        smallest_difficulty_target /= params.nPowTargetTimespan;

        if (smallest_difficulty_target > pow_limit) {
            smallest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 minimum_new_target;
        minimum_new_target.SetCompact(smallest_difficulty_target.GetCompact());
        if (minimum_new_target > observed_new_target) return false;
    } else {
        // Not at a retarget boundary
        if (old_nbits == new_nbits) {
            return true;
        }
        // Allow Emergency Difficulty Adjustment: new target = min(old target * 2, powLimit)
        // Only allow exactly 2x easing (or capped to powLimit), no arbitrary easing
        // EDA is disabled after height 200000
        if (params.fPowEnableEmergencyDifficultyDrop && (height < 200000)) {
            arith_uint256 old_target;
            old_target.SetCompact(old_nbits);
            arith_uint256 expected_new_target = old_target;
            expected_new_target <<= 1; // Double the target (halve difficulty)
            const arith_uint256 pow_limit = UintToArith256(params.powLimit);
            arith_uint256 expected_clamped = (expected_new_target > pow_limit) ? pow_limit : expected_new_target;
            
            arith_uint256 new_target;
            new_target.SetCompact(new_nbits);
            
            // Compare targets as arith_uint256 (not just compact ints)
            // Allow if new target equals expected EDA target (with powLimit clamp)
            if (new_target == expected_clamped) {
                return true;
            }
        }
        return false;
    }
    return true;
}

// Bypasses the actual proof of work check during fuzz testing with a simplified validation checking whether
// the most significant bit of the last byte of the hash is set.
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    if constexpr (G_FUZZING) return (hash.data()[31] & 0x80) == 0;
    return CheckProofOfWorkImpl(hash, nBits, params);
}

std::optional<arith_uint256> DeriveTarget(unsigned int nBits, const uint256 pow_limit)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(pow_limit))
        return {};

    return bnTarget;
}

bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    auto bnTarget{DeriveTarget(nBits, params.powLimit)};
    if (!bnTarget) return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
