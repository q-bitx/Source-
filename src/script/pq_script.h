// Copyright (c) 2025-present The Q-BitX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPT_PQ_SCRIPT_H
#define BITCOIN_SCRIPT_PQ_SCRIPT_H

#include <script/script.h>

/**
 * Check if a script matches the Q-BitX PQ P2PKH format.
 * Required format: OP_DUP OP_HASH160 <20-byte-hash> OP_EQUALVERIFY OP_CHECKSIGDILITHIUM
 * 
 * This function uses strict opcode parsing to validate:
 * - OP_DUP
 * - OP_HASH160
 * - Pushdata opcode pushing exactly 20 bytes
 * - OP_EQUALVERIFY
 * - OP_CHECKSIGDILITHIUM
 * - No trailing bytes
 * 
 * @param script The script to check
 * @return true if the script matches the PQ P2PKH format, false otherwise
 */
bool IsPQPayToPubKeyHash(const CScript& script);

#endif // BITCOIN_SCRIPT_PQ_SCRIPT_H
