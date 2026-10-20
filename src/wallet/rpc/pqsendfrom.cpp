// Copyright (c) 2025-present The Q-BitX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <addresstype.h>
#include <chainparams.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/params.h>
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
#include <wallet/pq_sign.h>
#include <wallet/pq_spend_weight.h>
#include <wallet/rpc/util.h>
#include <wallet/wallet.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <variant>
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

static bool IsNativePQWitnessProgram(const CScript& script)
{
    return script.size() == 22 && script[0] == OP_2 && script[1] == 20;
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

// Floor policy feerate at relay/mempool minimums so "low" remains relayable.
static CFeeRate ApplyRelayFloor(interfaces::Chain& chain, CFeeRate feerate)
{
    const CFeeRate floor = std::max(chain.relayMinFee(), chain.mempoolMinFee());
    if (feerate < floor) {
        return floor;
    }
    return feerate;
}

RPCHelpMan pqsendfrom()
{
    return RPCHelpMan{"pqsendfrom",
        "\nSend PQ (Dilithium) coins from a wallet address to another PQ address.\n"
        "Optional mode 'prepare' builds and signs a transaction without broadcasting.\n"
        "Default mode 'broadcast' preserves historical create+sign+broadcast behavior.\n"
        "The from_address must belong to the selected wallet.\n",
        {
            {"from_address", RPCArg::Type::STR, RPCArg::Optional::NO, "Source PQ/Dilithium address (must belong to this wallet)"},
            {"to_address", RPCArg::Type::STR, RPCArg::Optional::NO, "Destination PQ/Dilithium address"},
            {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Amount to send (in QBX)"},
            {"fee_policy", RPCArg::Type::STR, RPCArg::Default{"normal"}, "Fee policy: 'low' (1 sat/vB floored by relay/mempool min), 'normal' (5 sat/vB), or 'high' (15 sat/vB)"},
            {"mode", RPCArg::Type::STR, RPCArg::Default{"broadcast"}, "Transaction mode: 'broadcast' (default, historical behavior) or 'prepare' (sign only, no network send)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "txid", "The transaction ID"},
                {RPCResult::Type::STR_HEX, "rawtx", "The raw transaction hex"},
                {RPCResult::Type::NUM, "amount", "Amount sent"},
                {RPCResult::Type::NUM, "fee", "Transaction fee"},
                {RPCResult::Type::NUM, "change", "Change amount returned to sender"},
                {RPCResult::Type::STR, "mode", "Mode used: prepare or broadcast"},
                {RPCResult::Type::STR, "destination", "Destination address"},
                {RPCResult::Type::NUM, "weight", "Signed transaction weight"},
                {RPCResult::Type::NUM, "vsize", "Signed transaction virtual size"},
                {RPCResult::Type::ARR, "inputs", "Selected inputs",
                    {
                        {RPCResult::Type::OBJ, "", "",
                            {
                                {RPCResult::Type::STR_HEX, "txid", "Previous output txid"},
                                {RPCResult::Type::NUM, "vout", "Previous output index"},
                                {RPCResult::Type::NUM, "amount", "Input value"},
                            }},
                    }},
                {RPCResult::Type::ARR, "outputs", "Transaction outputs",
                    {
                        {RPCResult::Type::OBJ, "", "",
                            {
                                {RPCResult::Type::STR, "address", "Output address when decodable"},
                                {RPCResult::Type::NUM, "amount", "Output value"},
                            }},
                    }},
                {RPCResult::Type::BOOL, "broadcast", "True when the transaction was relayed"},
            }
        },
        RPCExamples{
            "\nSend 1.0 QBX from one PQ address to another\n"
            + HelpExampleCli("pqsendfrom", "\"<from_address>\" \"<to_address>\" 1.0") +
            "\nSend with explicit fee policy\n"
            + HelpExampleCli("pqsendfrom", "\"<from_address>\" \"<to_address>\" 5.0 \"normal\"") +
            "\nPrepare a signed transaction without broadcasting\n"
            + HelpExampleCli("pqsendfrom", "\"<from_address>\" \"<to_address>\" 1.0 \"low\" \"prepare\"")
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

        CKeyID keyid;
        if (const DilithiumPKHash* dil_pkhash = std::get_if<DilithiumPKHash>(&fromDest)) {
            keyid = CKeyID(uint160(*dil_pkhash));
        } else if (const DilithiumWitnessV0KeyHash* dil_wkh = std::get_if<DilithiumWitnessV0KeyHash>(&fromDest)) {
            keyid = CKeyID(uint160(*dil_wkh));
        } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                "from_address must be a Dilithium P2PKH or native PQ witness (pq-bech32) address");
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

        std::optional<int> tipHeightOptEarly = wallet->chain().getHeight();
        if (!tipHeightOptEarly.has_value()) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to get chain tip height");
        }
        const int next_height_early = *tipHeightOptEarly + 1;
        const Consensus::Params& consensusEarly{Params().GetConsensus()};
        const bool pq_witness_active = Consensus::IsPQWitnessEnabled(consensusEarly, next_height_early);
        const CScript changeScriptPubKey = pq_witness_active
            ? GetScriptForDestination(DilithiumWitnessV0KeyHash(dilPubKey))
            : fromScriptPubKey;

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
        CFeeRate feerate = ApplyRelayFloor(wallet->chain(), GetFeeRate(feePolicy));

        // Optional mode: default "broadcast" keeps historical behavior.
        std::string mode = "broadcast";
        if (request.params.size() > 4 && !request.params[4].isNull()) {
            mode = request.params[4].get_str();
        }
        if (mode != "broadcast" && mode != "prepare") {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "mode must be 'broadcast' or 'prepare'");
        }
        const bool prepare_only = (mode == "prepare");

        const int witness_scale = GetWitnessDiscountScale(consensusEarly, next_height_early);
        const bool legacy_scriptsig = !IsNativePQWitnessProgram(fromScriptPubKey);
        const std::vector<CScript> output_scripts{outputScriptPubKey, changeScriptPubKey};

        if (!pq_witness_active && (std::holds_alternative<DilithiumWitnessV0KeyHash>(toDest) || std::holds_alternative<DilithiumWitnessV0ScriptHash>(toDest))) {
            throw JSONRPCError(RPC_INVALID_REQUEST,
                strprintf("PQ witness is not active until height %d", consensusEarly.nPQWitnessHeight));
        }

        // Confirmed outputs only. Abandoned and conflicted spends do not
        // consume the outpoint; CWallet::IsSpent is the same check listunspent uses.
        std::vector<std::pair<COutPoint, CAmount>> availableUTXOs;
        std::set<COutPoint> seen_outpoints;

        for (const auto& [txid, wtx] : wallet->mapWallet) {
            int depth = wallet->GetTxDepthInMainChain(wtx);
            if (depth < 1) continue;

            const CTransaction& tx = *wtx.tx;

            for (uint32_t i = 0; i < tx.vout.size(); ++i) {
                const CTxOut& out = tx.vout[i];
                if (out.scriptPubKey != fromScriptPubKey) continue;
                if (!IsPQScript(out.scriptPubKey)) continue;
                if (wtx.IsCoinBase() && depth < COINBASE_MATURITY) continue;

                COutPoint outpoint(wtx.GetHash(), i);
                if (!seen_outpoints.insert(outpoint).second) continue;
                if (wallet->IsSpent(outpoint)) continue;
                // Skip coins locked by prepare-mode reservation or lockunspent.
                if (wallet->IsLockedCoin(outpoint)) continue;
                availableUTXOs.emplace_back(outpoint, out.nValue);
            }
        }

        if (availableUTXOs.empty()) {
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                "No mature coins available. Coinbase outputs require 100 confirmations.");
        }

        std::sort(availableUTXOs.begin(), availableUTXOs.end(),
            [](const std::pair<COutPoint, CAmount>& a, const std::pair<COutPoint, CAmount>& b) {
                return a.second > b.second;
            });

        std::vector<std::pair<COutPoint, CAmount>> selectedUTXOs;
        std::set<COutPoint> used_outpoints;
        CAmount totalInputAmount = 0;

        for (const auto& utxo : availableUTXOs) {
            if (used_outpoints.count(utxo.first)) continue;
            if (PqSpendExceedsStandardWeight(selectedUTXOs.size() + 1, legacy_scriptsig, witness_scale, output_scripts)) {
                break;
            }
            selectedUTXOs.push_back(utxo);
            used_outpoints.insert(utxo.first);
            totalInputAmount += utxo.second;
            const PqSpendWeight priced{EstimatePqSpendWeight(selectedUTXOs.size(), legacy_scriptsig, witness_scale, output_scripts)};
            if (totalInputAmount >= amount + feerate.GetFee(static_cast<uint32_t>(priced.vsize))) break;
        }

        if (selectedUTXOs.empty()) {
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                "No confirmed input fits the standard transaction weight limit.");
        }

        const std::vector<CScript> destination_only{outputScriptPubKey};
        const PqSpendWeight priced_two{EstimatePqSpendWeight(selectedUTXOs.size(), legacy_scriptsig, witness_scale, output_scripts)};
        CAmount fee = feerate.GetFee(static_cast<uint32_t>(priced_two.vsize));
        CAmount change = totalInputAmount - amount - fee;

        if (totalInputAmount < amount + fee) {
            const PqSpendWeight priced_one{EstimatePqSpendWeight(selectedUTXOs.size(), legacy_scriptsig, witness_scale, destination_only)};
            fee = feerate.GetFee(static_cast<uint32_t>(priced_one.vsize));
            change = 0;
        }

        CFeeRate dustRelayFee = wallet->chain().relayDustFee();
        if (change > 0 && IsDust(CTxOut(change, changeScriptPubKey), dustRelayFee)) {
            change = 0;
            fee = totalInputAmount - amount;
        }

        if (change < 0 || totalInputAmount < amount + fee) {
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

        // Build transaction
        CMutableTransaction mtx;
        
        // Add inputs
        for (const auto& [outpoint, value] : selectedUTXOs) {
            mtx.vin.emplace_back(outpoint, CScript{}, SEQ_NONFINAL);
        }
        
        // Add outputs
        mtx.vout.emplace_back(amount, outputScriptPubKey);
        if (change > 0) {
            mtx.vout.emplace_back(change, changeScriptPubKey);
        }

        std::map<COutPoint, Coin> pq_coins;
        for (const auto& [outpoint, value] : selectedUTXOs) {
            pq_coins.emplace(outpoint, Coin(CTxOut(value, fromScriptPubKey), 1, false));
        }
        bilingual_str pq_error;
        if (!SignPqWalletInputs(*wallet, mtx, pq_coins, pq_error)) {
            throw JSONRPCError(RPC_WALLET_ERROR, pq_error.original);
        }

        std::vector<CScript> signed_outputs{outputScriptPubKey};
        if (change > 0) signed_outputs.push_back(changeScriptPubKey);
        const PqSpendWeight estimated{EstimatePqSpendWeight(selectedUTXOs.size(), legacy_scriptsig, witness_scale, signed_outputs)};
        const PqSpendWeight actual{MeasurePqSpendWeight(CTransaction{mtx}, witness_scale)};
        if (actual.weight != estimated.weight || actual.vsize != estimated.vsize || actual.weight > MAX_STANDARD_TX_WEIGHT) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf(
                "Signed PQ transaction weight %d (vsize %d) does not match estimate %d (vsize %d) at scale %d",
                actual.weight, actual.vsize, estimated.weight, estimated.vsize, witness_scale));
        }

        CTransaction final_tx(mtx);
        Txid txid = final_tx.GetHash();
        std::string rawtxHex = EncodeHexTx(final_tx);

        UniValue inputs_arr(UniValue::VARR);
        for (const auto& [outpoint, value] : selectedUTXOs) {
            UniValue in(UniValue::VOBJ);
            in.pushKV("txid", outpoint.hash.GetHex());
            in.pushKV("vout", static_cast<int64_t>(outpoint.n));
            in.pushKV("amount", ValueFromAmount(value));
            inputs_arr.push_back(in);
        }

        UniValue outputs_arr(UniValue::VARR);
        {
            UniValue dest_out(UniValue::VOBJ);
            dest_out.pushKV("address", toAddress);
            dest_out.pushKV("amount", ValueFromAmount(amount));
            outputs_arr.push_back(dest_out);
            if (change > 0) {
                UniValue ch(UniValue::VOBJ);
                CTxDestination change_dest;
                if (ExtractDestination(changeScriptPubKey, change_dest)) {
                    ch.pushKV("address", EncodeDestination(change_dest));
                } else {
                    ch.pushKV("address", "");
                }
                ch.pushKV("amount", ValueFromAmount(change));
                outputs_arr.push_back(ch);
            }
        }

        if (prepare_only) {
            // Persist input locks so a restart cannot re-select the same UTXOs
            // for a second prepare of a different payout. Caller broadcasts via
            // sendrawtransaction after durable ledger commit.
            std::unique_ptr<WalletBatch> batch = std::make_unique<WalletBatch>(wallet->GetDatabase());
            for (const auto& [outpoint, value] : selectedUTXOs) {
                if (!wallet->LockCoin(outpoint, batch.get())) {
                    throw JSONRPCError(RPC_WALLET_ERROR, strprintf(
                        "Failed to lock prepared input %s:%u", outpoint.hash.GetHex(), outpoint.n));
                }
            }

            UniValue ret(UniValue::VOBJ);
            ret.pushKV("txid", txid.GetHex());
            ret.pushKV("rawtx", rawtxHex);
            ret.pushKV("amount", ValueFromAmount(amount));
            ret.pushKV("fee", ValueFromAmount(fee));
            if (change > 0) {
                ret.pushKV("change", ValueFromAmount(change));
            }
            ret.pushKV("mode", "prepare");
            ret.pushKV("destination", toAddress);
            ret.pushKV("weight", actual.weight);
            ret.pushKV("vsize", actual.vsize);
            ret.pushKV("inputs", inputs_arr);
            ret.pushKV("outputs", outputs_arr);
            ret.pushKV("broadcast", false);
            return ret;
        }

        // Historical path: sign+broadcast atomically.
        std::string err_string;
        const CAmount max_tx_fee = MAX_MONEY; // Accept any fee for regtest / operator policy
        bool broadcast_ok = wallet->chain().broadcastTransaction(MakeTransactionRef(final_tx), max_tx_fee, /*relay=*/true, err_string);
        if (!broadcast_ok) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Failed to broadcast transaction: %s", err_string));
        }
        wallet->AddToWallet(MakeTransactionRef(final_tx), TxStateInMempool{});

        UniValue ret(UniValue::VOBJ);
        ret.pushKV("txid", txid.GetHex());
        ret.pushKV("rawtx", rawtxHex);
        ret.pushKV("amount", ValueFromAmount(amount));
        ret.pushKV("fee", ValueFromAmount(fee));
        if (change > 0) {
            ret.pushKV("change", ValueFromAmount(change));
        }
        ret.pushKV("mode", "broadcast");
        ret.pushKV("destination", toAddress);
        ret.pushKV("weight", actual.weight);
        ret.pushKV("vsize", actual.vsize);
        ret.pushKV("inputs", inputs_arr);
        ret.pushKV("outputs", outputs_arr);
        ret.pushKV("broadcast", true);
        return ret;
    },
    };
}


} // namespace wallet
