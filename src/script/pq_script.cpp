// Copyright (c) 2025-present The Q-BitX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <script/pq_script.h>
#include <script/opcodes.h>
#include <script/solver.h>

bool IsPQPayToPubKeyHash(const CScript& script)
{
    CScript::const_iterator it = script.begin();
    opcodetype opcode;
    std::vector<unsigned char> data;
    
    // OP_DUP
    if (!script.GetOp(it, opcode) || opcode != OP_DUP) return false;
    
    // OP_HASH160
    if (!script.GetOp(it, opcode) || opcode != OP_HASH160) return false;
    
    // <20-byte-hash> - must be exactly 20 bytes
    if (!script.GetOp(it, opcode, data)) return false;
    if (!IsPushdataOp(opcode) || data.size() != 20) return false;
    
    // OP_EQUALVERIFY
    if (!script.GetOp(it, opcode) || opcode != OP_EQUALVERIFY) return false;
    
    // OP_CHECKSIGDILITHIUM (actual opcode used by Q-BitX PQ addresses)
    if (!script.GetOp(it, opcode) || opcode != OP_CHECKSIGDILITHIUM) return false;
    
    // Must be at end of script (no trailing bytes)
    if (it != script.end()) return false;
    
    return true;
}
