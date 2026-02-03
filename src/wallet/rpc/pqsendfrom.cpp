// Copyright (c) 2025-present The Q-BitX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <addresstype.h>
#include <chainparams.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <core_io.h>
#include <crypto/dilithium.h>
#include <crypto/dilithium_key.h>
#include <interfaces/chain.h>
#include <key_io.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <script/interpreter.h>
#include <script/solver.h>
#include <univalue.h>
#include <util/moneystr.h>
#include <util/strencodings.h>
#include <wallet/rpc/util.h>
#include <wallet/wallet.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace wallet {

// Non-final sequence value for transaction inputs
constexpr uint32_t SEQ_NONFINAL = 0xFFFFFFFE;

static bool IsPQScript(const CScript& script)
{
    std::vector<std::vector<unsigned char>> solutions;
    TxoutType whichType = Solver(script, solutions);
    return whichType == TxoutType::DILITHIUM_PUBKEY ||
           whichType == TxoutType::DILITHIUM_PUBKEYHASH ||
           whichType == TxoutType::DILITHIUM_SCRIPTHASH ||
           whichType == TxoutType::DILITHIUM_WITNESS_V0_KEYHASH ||
           whichType == TxoutType::DILITHIUM_WITNESS_V0_SCRIPTHASH ||
           whichType == TxoutType::DILITHIUM_MULTISIG ||
           whichType == TxoutType::QBITX_DILITHIUM;
}

// Extract Dilithium public key from secret key bytes
// In some implementations, the secret key storage format may include the public key appended
// or the Dilithium secret key format itself may embed the public key
// Returns true if pubkey_bytes is populated, false otherwise
static bool ExtractDilithiumPubkeyFromSecretKey(const std::vector<unsigned char>& sk_bytes, 
                                                 std::vector<unsigned char>& pk_bytes)
{
    pk_bytes.clear();
    
    // Check if secret key format includes appended public key (stored as sk || pk)
    if (sk_bytes.size() == DILITHIUM_SECRETKEYBYTES + DILITHIUM_PUBLICKEYBYTES) {
        // Public key is appended after secret key
        pk_bytes.assign(sk_bytes.begin() + DILITHIUM_SECRETKEYBYTES, sk_bytes.end());
        return true;
    }
    
    // Check if secret key format is just the standard secret key (no public key embedded)
    // In standard Dilithium format, the secret key does not embed the public key directly
    // The public key must be computed from secret key components or retrieved separately
    // For now, return false to indicate extraction from secret key alone is not possible
    // The caller should retrieve public key from wallet storage
    return false;
}

static CFeeRate GetFeeRate(const std::string& fee_policy)
{
    CAmount sat_per_kvb; // satoshis per 1000 vbytes
    if (fee_policy == "low") {
        sat_per_kvb = 1 * 1000; // 1 sat/vB = 1000 sat/kvB
    } else if (fee_policy == "normal" || fee_policy.empty()) {
        sat_per_kvb = 5 * 1000; // 5 sat/vB = 5000 sat/kvB
    } else if (fee_policy == "high") {
        sat_per_kvb = 15 * 1000; // 15 sat/vB = 15000 sat/kvB
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid fee policy. Must be 'low', 'normal', or 'high'");
    }
    return CFeeRate(sat_per_kvb);
}

// Calculate deterministic vsize for PQ P2PKH transaction without dummy signatures
// Uses fixed Dilithium3 sizes: signature ~2420 bytes, pubkey ~1952 bytes
static int64_t CalculatePQTxVsize(size_t num_inputs, size_t num_outputs)
{
    // Base transaction overhead
    int64_t base_size = 10; // version(4) + locktime(4) + vin_count(1-9) + vout_count(1-9) varints
    if (num_inputs > 9) base_size += 1; // larger varint
    if (num_outputs > 9) base_size += 1;
    
    // Each input: prevout(36) + sequence(4) + scriptsig_size_varint + scriptsig
    // PQ P2PKH scriptsig: <sig_with_hashtype> <pubkey>
    // sig_with_hashtype: DILITHIUM_SIGNATUREBYTES + 1 byte hashtype
    // pubkey: DILITHIUM_PUBLICKEYBYTES
    const int64_t sig_size = DILITHIUM_SIGNATUREBYTES + 1; // signature + hashtype
    const int64_t pubkey_size = DILITHIUM_PUBLICKEYBYTES;
    
    // Pushdata overhead: if size > 75, need 1 extra byte; if > 255, need 2 extra bytes
    // Dilithium sizes are large, so we'll need PUSHDATA2 (3 bytes overhead each)
    const int64_t sig_push_overhead = 3; // PUSHDATA2 prefix for large push
    const int64_t pubkey_push_overhead = 3;
    const int64_t scriptsig_size = sig_push_overhead + sig_size + pubkey_push_overhead + pubkey_size;
    const int64_t scriptsig_size_varint = GetSizeOfCompactSize(scriptsig_size);
    
    // Per input: prevout(36) + sequence(4) + scriptsig_varint + scriptsig
    const int64_t input_size = 36 + 4 + scriptsig_size_varint + scriptsig_size;
    
    // Each output: value(8) + scriptPubKey_size_varint + scriptPubKey
    // PQ P2PKH scriptPubKey: OP_DUP OP_HASH160 <20> <hash20> OP_EQUALVERIFY OP_CHECKSIGDILITHIUM
    const int64_t p2pkh_output_size = 8 + 1 + 25; // value(8) + varint(1) + 25-byte script
    
    // Total size in bytes (non-segwit, no witness discount)
    int64_t total_size = base_size + (num_inputs * input_size) + (num_outputs * p2pkh_output_size);
    
    // Convert to vsize (for non-segwit, vsize = size)
    return total_size;
}

RPCHelpMan pqsendfrom()
{
    return RPCHelpMan{"pqsendfrom",
        "\nSend PQ (Dilithium) coins from a wallet address to another PQ address.\n"
        "This command is for regression testing (-regtest mode) only.\n"
        "The from_address must belong to the selected wallet.\n",
        {
            {"from_address", RPCArg::Type::STR, RPCArg::Optional::NO, "Source PQ/Dilithium address (must belong to this wallet)"},
            {"to_address", RPCArg::Type::STR, RPCArg::Optional::NO, "Destination PQ/Dilithium address"},
            {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Amount to send (in QBX)"},
            {"fee_policy", RPCArg::Type::STR, RPCArg::Default{"normal"}, "Fee policy: 'low' (1 sat/vB), 'normal' (5 sat/vB), or 'high' (15 sat/vB)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "txid", "The transaction ID"},
                {RPCResult::Type::STR_HEX, "rawtx", "The raw transaction hex"},
                {RPCResult::Type::NUM, "amount", "Amount sent"},
                {RPCResult::Type::NUM, "fee", "Transaction fee"},
                {RPCResult::Type::NUM, "change", "Change amount returned to sender"},
            }
        },
        RPCExamples{
            "\nSend 1.0 QBX from one PQ address to another\n"
            + HelpExampleCli("pqsendfrom", "\"<from_address>\" \"<to_address>\" 1.0") +
            "\nSend with explicit fee policy\n"
            + HelpExampleCli("pqsendfrom", "\"<from_address>\" \"<to_address>\" 5.0 \"normal\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
    {

        std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
        if (!wallet) {
            throw JSONRPCError(RPC_WALLET_NOT_FOUND, "No wallet selected. Use -rpcwallet=<walletname> to specify which wallet to use.");
        }

        LOCK(wallet->cs_wallet);

        // Parse from_address
        std::string fromAddress = request.params[0].get_str();
        CTxDestination fromDest = DecodeDestination(fromAddress);
        if (!IsValidDestination(fromDest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, 
                strprintf("Invalid from_address: '%s'. Please provide a valid PQ/Dilithium address that belongs to this wallet.", fromAddress));
        }

        // Verify from_address is PQ/Dilithium
        CScript fromScriptPubKey = GetScriptForDestination(fromDest);
        if (!IsPQScript(fromScriptPubKey)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, 
                strprintf("from_address must be a PQ/Dilithium address. '%s' is not a PQ address. Use getnewaddress with address_type='pq' to create a PQ address.", fromAddress));
        }

        // Extract CKeyID from DilithiumPKHash address
        CKeyID keyid;
        if (const DilithiumPKHash* dil_pkhash = std::get_if<DilithiumPKHash>(&fromDest)) {
            keyid = CKeyID(uint160(*dil_pkhash));
        } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "from_address must be a Dilithium P2PKH address");
        }

        // Retrieve Dilithium private and public keys from wallet's DescriptorScriptPubKeyMan
        std::vector<unsigned char> privkeyBytes;
        std::vector<unsigned char> pubkeyBytes;
        bool foundKey = false;
        
        for (ScriptPubKeyMan* spkm : wallet->GetAllScriptPubKeyMans()) {
            DescriptorScriptPubKeyMan* desc_spkm = dynamic_cast<DescriptorScriptPubKeyMan*>(spkm);
            if (desc_spkm && desc_spkm->GetDilithiumKeys(keyid, privkeyBytes, pubkeyBytes)) {
                foundKey = true;
                break;
            }
        }

        if (!foundKey) {
            throw JSONRPCError(RPC_WALLET_ERROR, 
                strprintf("The address '%s' does not belong to this wallet. "
                         "Please use an address that was created with getnewaddress (or getnewaddress \"\" \"pq\") in this wallet. "
                         "If you have multiple wallets loaded, use -rpcwallet=<walletname> to select the correct wallet.",
                         fromAddress));
        }

        // For unencrypted keys, public key may need to be retrieved from wallet storage
        if (pubkeyBytes.empty()) {
            // First try to extract from secret key format if embedded (check if sk_bytes contains appended pk)
            std::vector<unsigned char> extractedPk;
            if (ExtractDilithiumPubkeyFromSecretKey(privkeyBytes, extractedPk)) {
                pubkeyBytes = extractedPk;
            } else if (wallet->map_dilithium_pub.count(keyid) > 0) {
                // Try wallet's map_dilithium_pub (legacy wallets)
                pubkeyBytes = wallet->map_dilithium_pub[keyid];
            } else {
                // Try reading from database as last resort
                std::unique_ptr<DatabaseBatch> db_batch = wallet->GetDatabase().MakeBatch();
                std::vector<unsigned char> db_pubkey;
                if (db_batch->Read(std::make_pair(DBKeys::DILITHIUM_PUBKEY, keyid), db_pubkey)) {
                    pubkeyBytes = db_pubkey;
                    wallet->map_dilithium_pub[keyid] = pubkeyBytes; // Cache it
                } else {
                    std::string walletType = wallet->IsWalletFlagSet(WALLET_FLAG_DESCRIPTORS) ? "descriptor" : "legacy";
                    throw JSONRPCError(RPC_WALLET_ERROR, 
                        strprintf("Dilithium public key not found for from_address (keyid=%s, wallet_type=%s). "
                                 "Ensure the address was generated with getnewdilithiumaddress in this wallet.",
                                 HexStr(keyid), walletType));
                }
            }
        }

        // Validate public key matches the address (safety check)
        if (pubkeyBytes.size() != DILITHIUM_PUBLICKEYBYTES) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Invalid Dilithium public key size: expected %u bytes, got %u", 
                static_cast<unsigned int>(DILITHIUM_PUBLICKEYBYTES), static_cast<unsigned int>(pubkeyBytes.size())));
        }
        
        CDilithiumPubKey dilPubKey(pubkeyBytes);
        if (!dilPubKey.IsValid()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Invalid Dilithium public key retrieved from wallet");
        }
        DilithiumPKHash derivedPkhash(dilPubKey);
        CKeyID derivedKeyid = CKeyID(uint160(derivedPkhash));
        if (derivedKeyid != keyid) {
            throw JSONRPCError(RPC_WALLET_ERROR, 
                strprintf("Public key from wallet does not match from_address (expected keyid=%s, got keyid=%s). "
                         "This may indicate a database corruption or key storage mismatch.",
                         HexStr(keyid), HexStr(derivedKeyid)));
        }

        // Parse to_address
        std::string toAddress = request.params[1].get_str();
        CTxDestination toDest = DecodeDestination(toAddress);
        if (!IsValidDestination(toDest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, 
                strprintf("Invalid destination address: '%s'. Please provide a valid PQ/Dilithium address.", toAddress));
        }

        // Verify to_address is PQ/Dilithium
        CScript outputScriptPubKey = GetScriptForDestination(toDest);
        if (!IsPQScript(outputScriptPubKey)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, 
                strprintf("to_address must be a PQ/Dilithium address. '%s' is not a PQ address. Use getnewaddress with address_type='pq' to create a PQ address.", toAddress));
        }

        // Parse amount
        CAmount amount = AmountFromValue(request.params[2]);
        if (amount <= 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Amount must be greater than 0");
        }

        // Parse fee policy
        std::string feePolicy = "normal";
        if (request.params.size() > 3 && !request.params[3].isNull()) {
            feePolicy = request.params[3].get_str();
        }
        CFeeRate feerate = GetFeeRate(feePolicy);

        // Verify chain state is available using wallet's chain interface (not NodeContext)
        // Note: wallet RPC requests may not carry NodeContext, so we use wallet.chain() instead
        std::optional<int> tipHeightOpt = wallet->chain().getHeight();
        if (!tipHeightOpt.has_value()) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to get chain tip height");
        }
        // tipHeight validated but not used directly - we use depth-based confirmations instead

        // Find UTXOs for the from_address scriptPubKey using wallet's listunspent-like functionality
        // For MVP, we'll use a simple approach: iterate wallet transactions and filter for PQ outputs
        std::vector<std::pair<COutPoint, CAmount>> availableUTXOs;
        
        for (const auto& [txid, wtx] : wallet->mapWallet) {
            int depth = wallet->GetTxDepthInMainChain(wtx);
            if (depth < 0) continue; // Not in chain
            
            const CTransaction& tx = *wtx.tx;
            
            for (uint32_t i = 0; i < tx.vout.size(); ++i) {
                const CTxOut& out = tx.vout[i];
                
                // Check if this output matches our scriptPubKey
                if (out.scriptPubKey != fromScriptPubKey) continue;
                
                // Check if it's a PQ script
                if (!IsPQScript(out.scriptPubKey)) continue;
                
                // Skip immature coinbase outputs (depth = confirmations from GetTxDepthInMainChain)
                if (wtx.IsCoinBase() && depth < COINBASE_MATURITY) {
                    continue; // Not matured
                }
                
                // Check if already spent (simple check: look for spends in wallet)
                // Use wtx.GetHash() to get Txid for COutPoint construction
                Txid wtxTxid = wtx.GetHash();
                COutPoint outpoint(wtxTxid, i);
                bool isSpent = false;
                for (const auto& [spendTxid, spendWtx] : wallet->mapWallet) {
                    const CTransaction& spendTx = *spendWtx.tx;
                    for (const auto& vin : spendTx.vin) {
                        if (vin.prevout == outpoint) {
                            isSpent = true;
                            break;
                        }
                    }
                    if (isSpent) break;
                }
                
                if (!isSpent) {
                    availableUTXOs.emplace_back(outpoint, out.nValue);
                }
            }
        }

        if (availableUTXOs.empty()) {
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                "No mature coins available. Coinbase outputs require 100 confirmations.");
        }

        // Simple UTXO selection: pick first one if sufficient, otherwise accumulate
        std::vector<std::pair<COutPoint, CAmount>> selectedUTXOs;
        CAmount totalInputAmount = 0;
        
        // Estimate initial fee using deterministic vsize calculation (1 input, 2 outputs: amount + change)
        int64_t estimatedVsize = CalculatePQTxVsize(1, 2);
        CAmount estimatedFee = feerate.GetFee(static_cast<uint32_t>(estimatedVsize));
        CAmount requiredAmount = amount + estimatedFee;
        
        for (const auto& [outpoint, value] : availableUTXOs) {
            selectedUTXOs.emplace_back(outpoint, value);
            totalInputAmount += value;
            
            // Stop if we have enough (simple policy: use first if sufficient)
            if (totalInputAmount >= requiredAmount) {
                break;
            }
        }

        // Recalculate fee with actual number of inputs
        int64_t numOutputs = 1; // at least the destination output
        CAmount change = totalInputAmount - amount;
        if (change > 0) {
            numOutputs = 2; // destination + change
        }
        int64_t vsize = CalculatePQTxVsize(selectedUTXOs.size(), numOutputs);
        CAmount fee = feerate.GetFee(static_cast<uint32_t>(vsize));
        
        // Recalculate change with actual fee
        change = totalInputAmount - amount - fee;
        
        // Check if change is dust
        CFeeRate dustRelayFee = wallet->chain().relayDustFee();
        bool changeIsDust = false;
        if (change > 0) {
            CTxOut changeOutput(change, fromScriptPubKey);
            changeIsDust = IsDust(changeOutput, dustRelayFee);
        }
        
        // If insufficient funds or change is dust
        if (totalInputAmount < amount + fee) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                strprintf("Insufficient funds: available %s, required %s (amount %s + fee %s). "
                         "Note: Immature coinbase transactions are not spendable until they have %d confirmations. "
                         "Use getaddressbalances to see confirmed, unconfirmed, and immature balances.",
                    FormatMoney(totalInputAmount),
                    FormatMoney(amount + fee),
                    FormatMoney(amount),
                    FormatMoney(fee),
                    COINBASE_MATURITY));
        }
        
        if (changeIsDust) {
            change = 0; // Don't include dust change
        }

        // Build transaction
        CMutableTransaction mtx;
        
        // Add inputs
        for (const auto& [outpoint, value] : selectedUTXOs) {
            mtx.vin.emplace_back(outpoint, CScript{}, SEQ_NONFINAL);
        }
        
        // Add outputs
        mtx.vout.emplace_back(amount, outputScriptPubKey);
        if (change > 0) {
            mtx.vout.emplace_back(change, fromScriptPubKey);
        }

        // Sign transaction using PQ signing
        CTransaction tx(mtx);
        int32_t nHashType = SIGHASH_ALL;
        CAmount amountForSighash = 0; // BASE mode doesn't use amount
        
        for (size_t i = 0; i < mtx.vin.size(); ++i) {
            uint256 sighash = SignatureHash(fromScriptPubKey, tx, i, nHashType, amountForSighash, SigVersion::BASE, nullptr);
            
            std::vector<unsigned char> msgHash(sighash.begin(), sighash.end());
            std::vector<unsigned char> signature;
            if (!PQ_Sign(signature, msgHash, privkeyBytes)) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign Dilithium transaction");
            }
            
            // Build scriptSig: <sig_with_hashtype> <pubkey>
            std::vector<unsigned char> sigWithHashtype = signature;
            sigWithHashtype.push_back(static_cast<unsigned char>(nHashType));
            
            CScript scriptSig;
            scriptSig << sigWithHashtype << pubkeyBytes;
            mtx.vin[i].scriptSig = scriptSig;
        }

        // Broadcast transaction using wallet's chain interface (not NodeContext)
        CTransaction final_tx(mtx);
        Txid txid = final_tx.GetHash();
        std::string rawtxHex = EncodeHexTx(final_tx);
        
        std::string err_string;
        const CAmount max_tx_fee = MAX_MONEY; // Accept any fee for regtest
        bool broadcast_ok = wallet->chain().broadcastTransaction(MakeTransactionRef(final_tx), max_tx_fee, /*relay=*/true, err_string);
        if (!broadcast_ok) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Failed to broadcast transaction: %s", err_string));
        }

        UniValue ret(UniValue::VOBJ);
        ret.pushKV("txid", txid.GetHex());
        ret.pushKV("rawtx", rawtxHex);
        ret.pushKV("amount", ValueFromAmount(amount));
        ret.pushKV("fee", ValueFromAmount(fee));
        if (change > 0) {
            ret.pushKV("change", ValueFromAmount(change));
        }
        
        return ret;
    },
    };
}

} // namespace wallet
