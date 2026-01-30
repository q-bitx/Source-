// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Q-BitX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef QBITX_OUTPUTTYPE_H
#define QBITX_OUTPUTTYPE_H

#include <addresstype.h>
#include <script/signingprovider.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

enum class OutputType {
    LEGACY,
    P2SH_SEGWIT,
    BECH32,
    BECH32M,
    DILITHIUM_LEGACY,
    DILITHIUM_BECH32,
    UNKNOWN,
};

static constexpr auto OUTPUT_TYPES = std::array{
    OutputType::LEGACY,
    OutputType::P2SH_SEGWIT,
    OutputType::BECH32,
    OutputType::BECH32M,
    OutputType::DILITHIUM_LEGACY,
    OutputType::DILITHIUM_BECH32,
};

// Wallet-only output types (excludes Dilithium types)
static constexpr auto WALLET_OUTPUT_TYPES = std::array{
    OutputType::LEGACY,
    OutputType::P2SH_SEGWIT,
    OutputType::BECH32,
    OutputType::BECH32M,
};

/** Check if an output type is supported by the standard wallet descriptor pipeline */
constexpr bool IsWalletSupportedOutputType(OutputType t) {
    return t == OutputType::LEGACY ||
           t == OutputType::P2SH_SEGWIT ||
           t == OutputType::BECH32 ||
           t == OutputType::BECH32M;
}

std::optional<OutputType> ParseOutputType(const std::string& str);
const std::string& FormatOutputType(OutputType type);

/**
 * Get a destination of the requested type (if possible) to the specified key.
 * The caller must make sure LearnRelatedScripts has been called beforehand.
 */
CTxDestination GetDestinationForKey(const CPubKey& key, OutputType);

/** Get all destinations (potentially) supported by the wallet for the given key. */
std::vector<CTxDestination> GetAllDestinationsForKey(const CPubKey& key);

/**
 * Get a destination of the requested type (if possible) to the specified script.
 * This function will automatically add the script (and any other
 * necessary scripts) to the keystore.
 */
CTxDestination AddAndGetDestinationForScript(FillableSigningProvider& keystore, const CScript& script, OutputType);

/** Get the OutputType for a CTxDestination */
std::optional<OutputType> OutputTypeFromDestination(const CTxDestination& dest);

#endif // QBITX_OUTPUTTYPE_H
