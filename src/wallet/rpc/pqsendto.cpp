// Copyright (c) 2025-present The Q-BitX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <addresstype.h>
#include <chainparams.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/params.h>
#include <consensus/validation.h>
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

static bool IsNativePQWitnessProgram(const CScript& script)
{
    // Native PQ witness keyhash discriminator: OP_2 <20-byte program>
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

// output_scripts is the planned destination plus change. The 2-output member
// is what selection and the standard-weight ceiling use. A no-change sweep
// prices the destination alone. Both paths go through EstimatePqSpendWeight,
// which builds a signed-size skeleton and calls GetTransactionWeightWithScale.
static std::vector<CScript> PqDestinationOnly(const std::vector<CScript>& output_scripts)
{
    if (output_scripts.empty()) return {};
    return {output_scripts.front()};
}

// Fewest already-sorted inputs that cover remaining plus the 2-output fee.
// Stops at MAX_INPUTS_PER_TX, or earlier when another input of this satisfaction
// type would make the transaction non-standard. Legacy scriptSig and native
// witness use different skeletons, so the ceiling is not the same input count.
std::vector<std::pair<COutPoint, CAmount>> SelectMinimumPqInputs(
    const std::vector<std::pair<COutPoint, CAmount>>& available,
    const std::set<COutPoint>& used,
    CAmount remaining,
    const CFeeRate& feerate,
    bool legacy_scriptsig,
    int witness_scale,
    const std::vector<CScript>& output_scripts)
{
    std::vector<std::pair<COutPoint, CAmount>> selected;
    CAmount total = 0;
    for (const auto& utxo : available) {
        if (used.count(utxo.first)) continue;
        if (PqSpendExceedsStandardWeight(selected.size() + 1, legacy_scriptsig, witness_scale, output_scripts)) {
            break;
        }
        selected.push_back(utxo);
        total += utxo.second;
        const PqSpendWeight priced{EstimatePqSpendWeight(selected.size(), legacy_scriptsig, witness_scale, output_scripts)};
        const CAmount fee2 = feerate.GetFee(static_cast<uint32_t>(priced.vsize));
        if (total >= remaining + fee2) break;
        if (selected.size() >= MAX_INPUTS_PER_TX) break;
    }
    return selected;
}

// Fees for the batches this send would actually build, not for every wallet UTXO.
CAmount EstimateMinimumPqSendFees(
    const std::vector<std::pair<COutPoint, CAmount>>& available,
    CAmount amount,
    const CFeeRate& feerate,
    bool legacy_scriptsig,
    int witness_scale,
    const std::vector<CScript>& output_scripts)
{
    std::set<COutPoint> used;
    CAmount remaining = amount;
    CAmount total_fees = 0;
    const std::vector<CScript> destination_only{PqDestinationOnly(output_scripts)};
    while (remaining > 0) {
        const auto selected = SelectMinimumPqInputs(available, used, remaining, feerate, legacy_scriptsig, witness_scale, output_scripts);
        if (selected.empty()) break;
        CAmount total = 0;
        for (const auto& utxo : selected) {
            total += utxo.second;
        }
        const PqSpendWeight two{EstimatePqSpendWeight(selected.size(), legacy_scriptsig, witness_scale, output_scripts)};
        const CAmount fee2 = feerate.GetFee(static_cast<uint32_t>(two.vsize));
        CAmount sent;
        CAmount fee;
        if (total < remaining + fee2) {
            const PqSpendWeight one{EstimatePqSpendWeight(selected.size(), legacy_scriptsig, witness_scale, destination_only)};
            fee = feerate.GetFee(static_cast<uint32_t>(one.vsize));
            sent = total - fee;
        } else {
            sent = remaining;
            fee = fee2;
        }
        if (sent <= 0) break;
        total_fees += fee;
        remaining -= sent;
        for (const auto& utxo : selected) {
            used.insert(utxo.first);
        }
    }
    return total_fees;
}

// pqsendtoaddress splits large sends into multiple transactions
// each using <=16 confirmed inputs to avoid oversized tx
// and mempool chain issues. No consensus logic is changed.
RPCHelpMan pqsendtoaddress()
{
    return RPCHelpMan{"pqsendtoaddress",
        "\nSend PQ (Dilithium) coins from a wallet address to another PQ address.\n"
        "Splits into multiple transactions when more than 16 inputs are needed, or when another legacy input would exceed the standard transaction weight limit.\n"
        "Available on mainnet and test networks. This RPC may create multiple transactions; use sendmany for an atomic multi-recipient send.\n"
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
                                {RPCResult::Type::NUM, "inputs_used", "Number of inputs used"},
                                {RPCResult::Type::BOOL, "debug_native_pq_witness", "True when the spent output is a native PQ witness"},
                                {RPCResult::Type::STR_HEX, "debug_scriptSig_hex", "scriptSig of the first input"},
                                {RPCResult::Type::NUM, "debug_witness_items", "Witness stack size of the first input"},
                                {RPCResult::Type::NUM, "debug_witness0_size", "Size of witness item 0"},
                                {RPCResult::Type::NUM, "debug_witness1_size", "Size of witness item 1"},
                                {RPCResult::Type::STR_HEX, "debug_txid", "txid"},
                                {RPCResult::Type::STR_HEX, "debug_wtxid", "wtxid"}
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
        } else if (const DilithiumWitnessV0KeyHash* dil_wkh = std::get_if<DilithiumWitnessV0KeyHash>(&fromDest)) {
            keyid = CKeyID(uint160(*dil_wkh));
        } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                "from_address must be a Dilithium P2PKH or native PQ witness (pq-bech32) address");
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

        std::optional<int> tipHeightOpt = wallet->chain().getHeight();
        if (!tipHeightOpt.has_value()) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to get chain tip height");
        }
        const int next_height = *tipHeightOpt + 1;
        const Consensus::Params& consensus{Params().GetConsensus()};
        const bool pq_witness_active = Consensus::IsPQWitnessEnabled(consensus, next_height);
        const CScript changeScriptPubKey = pq_witness_active
            ? GetScriptForDestination(DilithiumWitnessV0KeyHash(dilPubKey))
            : fromScriptPubKey;

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

        if (!pq_witness_active && (std::holds_alternative<DilithiumWitnessV0KeyHash>(toDest) || std::holds_alternative<DilithiumWitnessV0ScriptHash>(toDest))) {
            throw JSONRPCError(RPC_INVALID_REQUEST,
                strprintf("PQ witness is not active until height %d", consensus.nPQWitnessHeight));
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
        const int witness_scale = GetWitnessDiscountScale(consensus, next_height);
        const bool legacy_scriptsig = !IsNativePQWitnessProgram(fromScriptPubKey);

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
                // Abandoned, conflicted, and mempool-conflicted spends do not
                // consume the outpoint. CWallet::IsSpent is the same check
                // listunspent uses; a raw mapWallet scan treats those spends
                // as final.
                if (!wallet->IsSpent(outpoint)) {
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

        // Destination plus change. Selection prices this pair. A no-change sweep
        // prices the destination alone. Native change is a different script
        // from a legacy source, so the skeleton must use these exact scripts.
        const std::vector<CScript> output_scripts{outputScriptPubKey, changeScriptPubKey};
        const std::vector<CScript> destination_only{PqDestinationOnly(output_scripts)};

        // Fee for the inputs this amount actually requires. A small payment
        // must not be priced as if every matching UTXO were spent.
        CAmount estimatedTotalFees = EstimateMinimumPqSendFees(availableUTXOs, amount, feerate, legacy_scriptsig, witness_scale, output_scripts);

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

            selectedUTXOs = SelectMinimumPqInputs(availableUTXOs, usedOutpoints, remaining, feerate, legacy_scriptsig, witness_scale, output_scripts);
            for (const auto& [outpoint, value] : selectedUTXOs) {
                totalInputAmount += value;
            }

            if (selectedUTXOs.empty()) {
                throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                    strprintf("Insufficient funds: need %s more, already sent %s.",
                        FormatMoney(remaining), FormatMoney(totalSent)));
            }

            // Fee for 1 output (dest only) or 2 (dest + change), from the
            // signed-size skeleton at the next block's witness scale.
            const PqSpendWeight priced_one{EstimatePqSpendWeight(selectedUTXOs.size(), legacy_scriptsig, witness_scale, destination_only)};
            const int64_t vsize1 = priced_one.vsize;
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
                const PqSpendWeight priced_two{EstimatePqSpendWeight(selectedUTXOs.size(), legacy_scriptsig, witness_scale, output_scripts)};
                const int64_t vsize2 = priced_two.vsize;
                fee = feerate.GetFee(static_cast<uint32_t>(vsize2));
                change = totalInputAmount - sendAmount - fee;

                if (change > 0) {
                    CTxOut changeOutput(change, changeScriptPubKey);
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
                mtx.vout.emplace_back(change, changeScriptPubKey);
            }

            // Sign
            CTransaction tx(mtx);
            int32_t nHashType = SIGHASH_ALL;
            const bool from_is_native_pq_witness = IsNativePQWitnessProgram(fromScriptPubKey);
            CScript witness_script;
            if (from_is_native_pq_witness) {
                const std::vector<unsigned char> program(fromScriptPubKey.begin() + 2, fromScriptPubKey.end());
                witness_script << OP_DUP << OP_HASH160 << program << OP_EQUALVERIFY << OP_CHECKSIG;
            }
            for (size_t i = 0; i < mtx.vin.size(); ++i) {
                const CAmount input_amount = selectedUTXOs[i].second;
                const uint256 sighash = from_is_native_pq_witness
                    ? SignatureHash(witness_script, tx, i, nHashType, input_amount, SigVersion::WITNESS_V0, nullptr)
                    : SignatureHash(fromScriptPubKey, tx, i, nHashType, 0, SigVersion::BASE, nullptr);
                std::vector<unsigned char> msgHash(sighash.begin(), sighash.end());
                std::vector<unsigned char> signature;
                if (!PQ_Sign(signature, msgHash, privkeyBytes)) {
                    throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign Dilithium transaction");
                }
                std::vector<unsigned char> sigWithHashtype = signature;
                sigWithHashtype.push_back(static_cast<unsigned char>(nHashType));
                if (from_is_native_pq_witness) {
                    mtx.vin[i].scriptSig = CScript{};
                    mtx.vin[i].scriptWitness.stack.clear();
                    mtx.vin[i].scriptWitness.stack.push_back(sigWithHashtype);
                    mtx.vin[i].scriptWitness.stack.push_back(pubkeyBytes);
                } else {
                    CScript scriptSig;
                    scriptSig << sigWithHashtype << pubkeyBytes;
                    mtx.vin[i].scriptSig = scriptSig;
                    mtx.vin[i].scriptWitness.stack.clear();
                }
            }

            const std::string debug_script_sig_hex = mtx.vin.empty() ? "" : HexStr(mtx.vin[0].scriptSig);
            const int64_t debug_witness_items = mtx.vin.empty() ? 0 : static_cast<int64_t>(mtx.vin[0].scriptWitness.stack.size());
            const int64_t debug_witness0_size =
                (!mtx.vin.empty() && mtx.vin[0].scriptWitness.stack.size() > 0) ? static_cast<int64_t>(mtx.vin[0].scriptWitness.stack[0].size()) : 0;
            const int64_t debug_witness1_size =
                (!mtx.vin.empty() && mtx.vin[0].scriptWitness.stack.size() > 1) ? static_cast<int64_t>(mtx.vin[0].scriptWitness.stack[1].size()) : 0;
            // The signed transaction must match the skeleton. A shorter
            // signature would overpay; a longer one would underpay and can
            // also cross MAX_STANDARD_TX_WEIGHT after the selection check.
            std::vector<CScript> signed_outputs{outputScriptPubKey};
            if (change > 0) signed_outputs.push_back(changeScriptPubKey);
            const PqSpendWeight estimated{EstimatePqSpendWeight(selectedUTXOs.size(), legacy_scriptsig, witness_scale, signed_outputs)};
            const CTransaction preview{mtx};
            const PqSpendWeight actual{MeasurePqSpendWeight(preview, witness_scale)};
            if (actual.weight != estimated.weight || actual.vsize != estimated.vsize || actual.weight > MAX_STANDARD_TX_WEIGHT) {
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf(
                    "Signed PQ transaction weight %d (vsize %d) does not match estimate %d (vsize %d) at scale %d",
                    actual.weight, actual.vsize, estimated.weight, estimated.vsize, witness_scale));
            }

            // Broadcast: construct final tx by moving mtx so witness stacks are preserved as-is.
            CTransactionRef final_tx = MakeTransactionRef(std::move(mtx));
            Txid txid = final_tx->GetHash();
            Wtxid wtxid = final_tx->GetWitnessHash();
            std::string err_string;
            const CAmount max_tx_fee = MAX_MONEY;
            bool broadcast_ok = wallet->chain().broadcastTransaction(final_tx, max_tx_fee, /*relay=*/true, err_string);
            if (!broadcast_ok) {
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf(
                    "Failed to broadcast transaction: %s (native_pq_witness=%d scriptSig=%s witness_items=%d witness0_size=%d witness1_size=%d txid=%s wtxid=%s)",
                    err_string,
                    from_is_native_pq_witness,
                    debug_script_sig_hex,
                    debug_witness_items,
                    debug_witness0_size,
                    debug_witness1_size,
                    txid.GetHex(),
                    wtxid.GetHex()));
            }
            // Record the spend before the RPC returns. The mempool callback also
            // does this, but it cannot take cs_wallet until this call finishes,
            // so a second pqsendtoaddress would otherwise select the same input.
            wallet->AddToWallet(final_tx, TxStateInMempool{});

            txidsArr.push_back(txid.GetHex());
            totalSent += sendAmount;
            totalFee += fee;

            // Add to details array
            UniValue detail(UniValue::VOBJ);
            detail.pushKV("txid", txid.GetHex());
            detail.pushKV("sent", ValueFromAmount(sendAmount));
            detail.pushKV("fee", ValueFromAmount(fee));
            detail.pushKV("inputs_used", static_cast<int64_t>(selectedUTXOs.size()));
            detail.pushKV("debug_native_pq_witness", from_is_native_pq_witness);
            detail.pushKV("debug_scriptSig_hex", debug_script_sig_hex);
            detail.pushKV("debug_witness_items", debug_witness_items);
            detail.pushKV("debug_witness0_size", debug_witness0_size);
            detail.pushKV("debug_witness1_size", debug_witness1_size);
            detail.pushKV("debug_txid", txid.GetHex());
            detail.pushKV("debug_wtxid", wtxid.GetHex());
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
