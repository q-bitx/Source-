// Copyright (c) 2026-present The Q-BitX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_PQ_SIGN_H
#define BITCOIN_WALLET_PQ_SIGN_H

#include <coins.h>
#include <primitives/transaction.h>
#include <util/translation.h>

#include <map>

namespace wallet {
class CWallet;
bool SignPqWalletInputs(const CWallet& wallet, CMutableTransaction& tx, const std::map<COutPoint, Coin>& coins, bilingual_str& error);
} // namespace wallet

#endif
