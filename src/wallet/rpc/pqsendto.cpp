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

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <vector>

namespace wallet {

// Maximum number of inputs per transaction to avoid oversized transactions
constexpr size_t MAX_INPUTS_PER_TX = 16;

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

// pqsendtoaddress splits large sends into multiple transactions
// each using <=16 confirmed inputs to avoid oversized tx
// and mempool chain issues. No consensus logic is changed.
RPCHelpMan pqsendtoaddress()
{
    return RPCHelpMan{"pqsendtoaddress",
        "\nSend PQ (Dilithium) coins from a wallet address to another PQ address.\n"
        "Splits into multiple transactions when more than 16 inputs are needed (max 16 inputs per tx).\n"
        "This command is for regression testing (-regtest mode) only.\n"
        "The from_address must belong to the selected wallet.\n",
        {
            {"from_address", RPCArg::Type::STR, RPCArg::Optional::NO, "Source PQ/Dilithium address (must belong to this wallet)"},
            {"to_address", RPCArg::Type::STR, RPCArg::Optional::NO, "Destination PQ/Dilithium address"},
            {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Amount to send (in QBX)"},
            {"fee_level", RPCArg::Type::STR, RPCArg::Default{"normal"}, "Fee level: 'low' (1 sat/vB), 'normal' (5 sat/vB), or 'high' (15 sat/vB)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::ARR, "txids", "Array of transaction IDs",
                    {
                        {RPCResult::Type::STR_HEX, "", "txid"}
                    }
                },
                {RPCResult::Type::NUM, "tx_count", "Number of transactions created"},
                {RPCResult::Type::NUM, "total_sent", "Total amount sent"},
                {RPCResult::Type::NUM, "total_fee", "Total fees paid"},
                {RPCResult::Type::ARR, "details", "Array of transaction details",
                    {
                        {RPCResult::Type::OBJ, "", "",
                            {
                                {RPCResult::Type::STR_HEX, "txid", "Transaction ID"},
                                {RPCResult::Type::NUM, "sent", "Amount sent in this transaction"},
                                {RPCResult::Type::NUM, "fee", "Fee paid for this transaction"},
                                {RPCResult::Type::NUM, "inputs_used", "Number of inputs used"}
                            }
                        }
                    }
                }
            }
        },
        RPCExamples{
            "\nSend 10.0 QBX from one PQ address to another\n"
            + HelpExampleCli("pqsendtoaddress", "\"<from_address>\" \"<to_address>\" 10.0") +
            "\nSend with explicit fee policy\n"
            + HelpExampleCli("pqsendtoaddress", "\"<from_address>\" \"<to_address>\" 5.0 \"normal\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
    {
        std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
        if (!wallet) {
            throw JSONRPCError(RPC_WALLET_NOT_FOUND, "No wallet selected. Use -rpcwallet=<walletname> to specify which wallet to use.");
        }

        LOCK(wallet->cs_wallet);

        // Parse from_address (same validation as pqsendfrom)
        std::string fromAddress = request.params[0].get_str();
        CTxDestination fromDest = DecodeDestination(fromAddress);
        if (!IsValidDestination(fromDest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                strprintf("Invalid from_address: '%s'. Please provide a valid PQ/Dilithium address that belongs to this wallet.", fromAddress));
        }

        CScript fromScriptPubKey = GetScriptForDestination(fromDest);
        if (!IsPQScript(fromScriptPubKey)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                strprintf("from_address must be a PQ/Dilithium address. '%s' is not a PQ address.", fromAddress));
        }

        CKeyID keyid;
        if (const DilithiumPKHash* dil_pkhash = std::get_if<DilithiumPKHash>(&fromDest)) {
            keyid = CKeyID(uint160(*dil_pkhash));
        } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "from_address must be a Dilithium P2PKH address");
        }

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
        // Legacy wallet fallback: keys stored in map_dilithium_priv / map_dilithium_pub
        if (!foundKey && wallet->map_dilithium_priv.count(keyid) > 0) {
            privkeyBytes = wallet->map_dilithium_priv[keyid];
            if (wallet->map_dilithium_pub.count(keyid) > 0) {
                pubkeyBytes = wallet->map_dilithium_pub[keyid];
            }
            foundKey = true;
        }

        if (!foundKey) {
            throw JSONRPCError(RPC_WALLET_ERROR,
                strprintf("The address '%s' does not belong to this wallet.", fromAddress));
        }

        if (pubkeyBytes.empty()) {
            std::vector<unsigned char> extractedPk;
            if (ExtractDilithiumPubkeyFromSecretKey(privkeyBytes, extractedPk)) {
                pubkeyBytes = extractedPk;
            } else if (wallet->map_dilithium_pub.count(keyid) > 0) {
                pubkeyBytes = wallet->map_dilithium_pub[keyid];
            } else {
                std::unique_ptr<DatabaseBatch> db_batch = wallet->GetDatabase().MakeBatch();
                std::vector<unsigned char> db_pubkey;
                if (db_batch->Read(std::make_pair(DBKeys::DILITHIUM_PUBKEY, keyid), db_pubkey)) {
                    pubkeyBytes = db_pubkey;
                    wallet->map_dilithium_pub[keyid] = pubkeyBytes;
                } else {
                    throw JSONRPCError(RPC_WALLET_ERROR, "Dilithium public key not found for from_address");
                }
            }
        }

        if (pubkeyBytes.size() != DILITHIUM_PUBLICKEYBYTES) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Invalid Dilithium public key size: expected %u, got %u",
                static_cast<unsigned int>(DILITHIUM_PUBLICKEYBYTES), static_cast<unsigned int>(pubkeyBytes.size())));
        }

        CDilithiumPubKey dilPubKey(pubkeyBytes);
        if (!dilPubKey.IsValid()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Invalid Dilithium public key");
        }
        DilithiumPKHash derivedPkhash(dilPubKey);
        CKeyID derivedKeyid = CKeyID(uint160(derivedPkhash));
        if (derivedKeyid != keyid) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Public key does not match from_address");
        }

        // Parse to_address
        std::string toAddress = request.params[1].get_str();
        CTxDestination toDest = DecodeDestination(toAddress);
        if (!IsValidDestination(toDest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                strprintf("Invalid destination address: '%s'.", toAddress));
        }

        CScript outputScriptPubKey = GetScriptForDestination(toDest);
        if (!IsPQScript(outputScriptPubKey)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                strprintf("to_address must be a PQ/Dilithium address. '%s' is not a PQ address.", toAddress));
        }

        CAmount amount = AmountFromValue(request.params[2]);
        if (amount <= 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Amount must be greater than 0");
        }

        std::string feeLevel = "normal";
        if (request.params.size() > 3 && !request.params[3].isNull()) {
            feeLevel = request.params[3].get_str();
        }
        CFeeRate feerate = GetFeeRate(feeLevel);

        std::optional<int> tipHeightOpt = wallet->chain().getHeight();
        if (!tipHeightOpt.has_value()) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to get chain tip height");
        }

        // Gather available UTXOs - only confirmed (depth >= 1), unspent
        std::vector<std::pair<COutPoint, CAmount>> availableUTXOs;
        for (const auto& [txid, wtx] : wallet->mapWallet) {
            int depth = wallet->GetTxDepthInMainChain(wtx);
            // Only use confirmed UTXOs (depth >= 1)
            if (depth < 1) continue;

            const CTransaction& tx = *wtx.tx;
            for (uint32_t i = 0; i < tx.vout.size(); ++i) {
                const CTxOut& out = tx.vout[i];
                if (out.scriptPubKey != fromScriptPubKey) continue;
                if (!IsPQScript(out.scriptPubKey)) continue;
                if (wtx.IsCoinBase() && depth < COINBASE_MATURITY) continue;

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
                "No confirmed coins available. Only UTXOs with at least 1 confirmation are used.");
        }

        // Sort by amount DESC to prefer larger UTXOs (minimize inputs)
        std::sort(availableUTXOs.begin(), availableUTXOs.end(),
            [](const std::pair<COutPoint, CAmount>& a, const std::pair<COutPoint, CAmount>& b) {
                return a.second > b.second;
            });

        CFeeRate dustRelayFee = wallet->chain().relayDustFee();

        // PLANNING PHASE: Estimate transactions needed and verify sufficient funds
        CAmount totalConfirmedFunds = 0;
        for (const auto& [outpoint, value] : availableUTXOs) {
            totalConfirmedFunds += value;
        }

        // Estimate total fees needed (conservative estimate: assume worst case)
        // Estimate with max inputs per tx and 2 outputs (dest + change)
        int64_t estimatedVsizePerTx = CalculatePQTxVsize(MAX_INPUTS_PER_TX, 2);
        CAmount estimatedFeePerTx = feerate.GetFee(static_cast<uint32_t>(estimatedVsizePerTx));
        // Estimate number of transactions needed (conservative: assume we need all inputs)
        size_t estimatedTxCount = (availableUTXOs.size() + MAX_INPUTS_PER_TX - 1) / MAX_INPUTS_PER_TX;
        CAmount estimatedTotalFees = estimatedFeePerTx * estimatedTxCount;

        if (totalConfirmedFunds < amount + estimatedTotalFees) {
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                strprintf("Insufficient confirmed funds: available %s, required %s (amount %s + estimated fees %s). "
                         "Only UTXOs with at least 1 confirmation are used.",
                    FormatMoney(totalConfirmedFunds),
                    FormatMoney(amount + estimatedTotalFees),
                    FormatMoney(amount),
                    FormatMoney(estimatedTotalFees)));
        }

        // Build and broadcast transactions
        std::set<COutPoint> usedOutpoints;
        CAmount remaining = amount;
        CAmount totalSent = 0;
        CAmount totalFee = 0;
        UniValue txidsArr(UniValue::VARR);
        UniValue detailsArr(UniValue::VARR);

        // Build and broadcast txs, each with up to MAX_INPUTS_PER_TX inputs
        while (remaining > 0) {
            std::vector<std::pair<COutPoint, CAmount>> selectedUTXOs;
            CAmount totalInputAmount = 0;

            for (const auto& [outpoint, value] : availableUTXOs) {
                if (usedOutpoints.count(outpoint)) continue;
                selectedUTXOs.emplace_back(outpoint, value);
                totalInputAmount += value;
                if (selectedUTXOs.size() >= MAX_INPUTS_PER_TX) break;
            }

            if (selectedUTXOs.empty()) {
                throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                    strprintf("Insufficient funds: need %s more, already sent %s.",
                        FormatMoney(remaining), FormatMoney(totalSent)));
            }

            // Fee for 1 output (dest only) or 2 (dest + change)
            int64_t vsize1 = CalculatePQTxVsize(selectedUTXOs.size(), 1);
            CAmount fee1 = feerate.GetFee(static_cast<uint32_t>(vsize1));
            CAmount maxSendableNoChange = totalInputAmount - fee1;

            CAmount sendAmount;
            CAmount change = 0;
            CAmount fee;

            if (maxSendableNoChange < remaining) {
                // Cannot cover remaining with change; send max (no change output)
                sendAmount = maxSendableNoChange;
                fee = fee1;
                change = 0;
            } else {
                // Can cover remaining; send remaining and possibly have change
                sendAmount = remaining;
                int64_t vsize2 = CalculatePQTxVsize(selectedUTXOs.size(), 2);
                fee = feerate.GetFee(static_cast<uint32_t>(vsize2));
                change = totalInputAmount - sendAmount - fee;

                if (change > 0) {
                    CTxOut changeOutput(change, fromScriptPubKey);
                    if (IsDust(changeOutput, dustRelayFee)) {
                        change = 0;
                        fee = totalInputAmount - sendAmount;
                    }
                }
            }

            if (sendAmount <= 0) {
                throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                    strprintf("Insufficient funds: inputs %s cannot cover fee. Need more UTXOs.",
                        FormatMoney(totalInputAmount)));
            }

            // Build transaction
            CMutableTransaction mtx;
            for (const auto& [outpoint, val] : selectedUTXOs) {
                mtx.vin.emplace_back(outpoint, CScript{}, SEQ_NONFINAL);
            }
            mtx.vout.emplace_back(sendAmount, outputScriptPubKey);
            if (change > 0) {
                mtx.vout.emplace_back(change, fromScriptPubKey);
            }

            // Sign
            CTransaction tx(mtx);
            int32_t nHashType = SIGHASH_ALL;
            CAmount amountForSighash = 0;
            for (size_t i = 0; i < mtx.vin.size(); ++i) {
                uint256 sighash = SignatureHash(fromScriptPubKey, tx, i, nHashType, amountForSighash, SigVersion::BASE, nullptr);
                std::vector<unsigned char> msgHash(sighash.begin(), sighash.end());
                std::vector<unsigned char> signature;
                if (!PQ_Sign(signature, msgHash, privkeyBytes)) {
                    throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign Dilithium transaction");
                }
                std::vector<unsigned char> sigWithHashtype = signature;
                sigWithHashtype.push_back(static_cast<unsigned char>(nHashType));
                CScript scriptSig;
                scriptSig << sigWithHashtype << pubkeyBytes;
                mtx.vin[i].scriptSig = scriptSig;
            }

            // Broadcast
            CTransaction final_tx(mtx);
            Txid txid = final_tx.GetHash();
            std::string err_string;
            const CAmount max_tx_fee = MAX_MONEY;
            bool broadcast_ok = wallet->chain().broadcastTransaction(MakeTransactionRef(final_tx), max_tx_fee, /*relay=*/true, err_string);
            if (!broadcast_ok) {
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Failed to broadcast transaction: %s", err_string));
            }

            txidsArr.push_back(txid.GetHex());
            totalSent += sendAmount;
            totalFee += fee;

            // Add to details array
            UniValue detail(UniValue::VOBJ);
            detail.pushKV("txid", txid.GetHex());
            detail.pushKV("sent", ValueFromAmount(sendAmount));
            detail.pushKV("fee", ValueFromAmount(fee));
            detail.pushKV("inputs_used", static_cast<int64_t>(selectedUTXOs.size()));
            detailsArr.push_back(detail);

            for (const auto& [op, val] : selectedUTXOs) {
                usedOutpoints.insert(op);
            }
            remaining -= sendAmount;
        }

        UniValue ret(UniValue::VOBJ);
        ret.pushKV("txids", txidsArr);
        ret.pushKV("tx_count", static_cast<int64_t>(txidsArr.size()));
        ret.pushKV("total_sent", ValueFromAmount(totalSent));
        ret.pushKV("total_fee", ValueFromAmount(totalFee));
        ret.pushKV("details", detailsArr);
        return ret;
    },
    };
}

} // namespace wallet
