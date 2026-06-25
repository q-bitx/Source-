// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_CONSENSUS_H
#define BITCOIN_CONSENSUS_CONSENSUS_H

#include <cstdlib>
#include <stdint.h>

/** BTQ-aligned block limits (active from nBlockLimitsUpgradeHeight onward). */
static constexpr unsigned int QBX_MAX_BLOCK_WEIGHT = 8000000;
static constexpr unsigned int QBX_MAX_BLOCK_SERIALIZED_SIZE = 8000000;

/** Legacy pre-upgrade consensus limits (height < nBlockLimitsUpgradeHeight). */
static constexpr unsigned int LEGACY_MAX_BLOCK_WEIGHT = 16000000;
/** Pre-upgrade P2P / getblocktemplate serialized-size guidance (not a consensus rule). */
static constexpr unsigned int LEGACY_MAX_BLOCK_P2P_SERIALIZED_SIZE = 4000000;

/**
 * Largest block that may be stored on disk or read during reindex.
 * Pre-upgrade blocks had no consensus serialized-size cap; weight was capped at LEGACY_MAX_BLOCK_WEIGHT.
 */
static constexpr unsigned int MAX_BLOCK_DISK_SERIALIZED_SIZE = LEGACY_MAX_BLOCK_WEIGHT;

/**
 * Backward-compatible aliases: upper bound for buffers and context-free sanity checks.
 * Height-aware consensus uses Consensus::GetMaxBlockWeight / GetMaxBlockSerializedSize.
 */
static const unsigned int MAX_BLOCK_SERIALIZED_SIZE = MAX_BLOCK_DISK_SERIALIZED_SIZE;
static const unsigned int MAX_BLOCK_WEIGHT = LEGACY_MAX_BLOCK_WEIGHT;
/** The maximum allowed number of signature check operations in a block (network rule) */
static const int64_t MAX_BLOCK_SIGOPS_COST = 80000;
/** Coinbase transaction outputs can only be spent after this number of new blocks (network rule) */
static const int COINBASE_MATURITY = 100;

/** Post-PQ-witness-activation witness discount (BTQ-style); see GetWitnessDiscountScale in validation.h */
static constexpr int PQ_WITNESS_SCALE_FACTOR = 16;

static const int WITNESS_SCALE_FACTOR = 4;

static const size_t MIN_TRANSACTION_WEIGHT = WITNESS_SCALE_FACTOR * 60; // 60 is the lower bound for the size of a valid serialized CTransaction
static const size_t MIN_SERIALIZABLE_TRANSACTION_WEIGHT = WITNESS_SCALE_FACTOR * 10; // 10 is the lower bound for the size of a serialized CTransaction

/** Flags for nSequence and nLockTime locks */
/** Interpret sequence numbers as relative lock-time constraints. */
static constexpr unsigned int LOCKTIME_VERIFY_SEQUENCE = (1 << 0);

/**
 * Maximum number of seconds that the timestamp of the first
 * block of a difficulty adjustment period is allowed to
 * be earlier than the last block of the previous period (BIP94).
 */
static constexpr int64_t MAX_TIMEWARP = 600;

#endif // BITCOIN_CONSENSUS_CONSENSUS_H
