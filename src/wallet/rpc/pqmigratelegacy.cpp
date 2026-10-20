// Copyright (c) 2026 The Q-BitX Core developers
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
#include <interfaces/chain.h>
#include <key_io.h>
#include <outputtype.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <script/interpreter.h>
#include <script/pq_script.h>
#include <script/solver.h>
#include <univalue.h>
#include <util/moneystr.h>
#include <util/result.h>
#include <wallet/pq_spend_weight.h>
#include <wallet/rpc/util.h>
#include <wallet/scriptpubkeyman.h>
#include <wallet/wallet.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace wallet {

namespace {

constexpr int MIGRATION_BATCH_CAP{25};
constexpr size_t MIGRATION_INPUT_SEARCH_CAP{64};

CFeeRate MigrationFeeRate(const std::string& fee_policy)
{
    CAmount sat_per_kvb{5 * 1000};
    if (fee_policy == "low") {
        sat_per_kvb = 1 * 1000;
    } else if (fee_policy == "normal" || fee_policy.empty()) {
        sat_per_kvb = 5 * 1000;
    } else if (fee_policy == "high") {
        sat_per_kvb = 15 * 1000;
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "fee_policy must be low, normal, or high");
    }
    return CFeeRate(sat_per_kvb);
}

struct LegacyCoin {
    COutPoint outpoint;
    CAmount value{0};
    CScript script;
    CKeyID keyid;
};

size_t InputsThatFit(int witness_scale, int weight_cap, const CScript& dest)
{
    size_t fit{0};
    for (size_t n{1}; n <= MIGRATION_INPUT_SEARCH_CAP; ++n) {
        const PqSpendWeight estimate{EstimatePqSpendWeight(n, /*legacy_scriptsig=*/true, witness_scale, {dest})};
        if (estimate.weight > weight_cap) break;
        fit = n;
    }
    return fit;
}

bool LookupLegacyKey(CWallet& wallet, const CKeyID& keyid, std::vector<unsigned char>& priv, std::vector<unsigned char>& pub)
{
    for (ScriptPubKeyMan* spkm : wallet.GetAllScriptPubKeyMans()) {
        auto* desc = dynamic_cast<DescriptorScriptPubKeyMan*>(spkm);
        if (desc && desc->GetDilithiumKeys(keyid, priv, pub)) {
            if (pub.empty() && wallet.map_dilithium_pub.count(keyid) > 0) {
                pub = wallet.map_dilithium_pub[keyid];
            }
            return !priv.empty() && pub.size() == DILITHIUM_PUBLICKEYBYTES;
        }
    }
    if (wallet.map_dilithium_priv.count(keyid) > 0 && wallet.map_dilithium_pub.count(keyid) > 0) {
        priv = wallet.map_dilithium_priv[keyid];
        pub = wallet.map_dilithium_pub[keyid];
        return pub.size() == DILITHIUM_PUBLICKEYBYTES;
    }
    return false;
}

} // namespace

RPCHelpMan pqmigratelegacy()
{
    return RPCHelpMan{
        "pqmigratelegacy",
        "\nConvert confirmed legacy Dilithium P2PKH outputs to one native PQ witness (dil1) output.\n"
        "This is a wallet helper for the permanent mempool migration policy. It has no end height and does not change consensus.\n"
        "source_address must be one legacy address, or the exact string \"all\".\n"
        "dry_run does not sign, reserve, broadcast, or create a destination. broadcast=false signs one batch and returns raw hex\n"
        "without reserving inputs: submit that hex yourself, and do not call the RPC again for those inputs\n"
        "until the transaction is in the mempool or a block. Old peers will not relay the loose transaction;\n"
        "send the hex with sendrawtransaction on an upgraded SoloPool node.\n",
        {
            {"source_address", RPCArg::Type::STR, RPCArg::Optional::NO, "Legacy Dilithium address, or \"all\" for every confirmed legacy UTXO in this wallet."},
            {"destination_address", RPCArg::Type::STR, RPCArg::Default{""}, "Native dil1 address. Empty creates a new wallet address when dry_run is false."},
            {"max_batches", RPCArg::Type::NUM, RPCArg::Default{10}, "Maximum transactions to build in this call (1..25). broadcast=false always builds one."},
            {"max_tx_weight", RPCArg::Type::NUM, RPCArg::Default{MIGRATION_MAX_STANDARD_WEIGHT}, "Per-transaction weight cap. Cannot exceed the active policy cap."},
            {"fee_policy", RPCArg::Type::STR, RPCArg::Default{"normal"}, "low (1 sat/vB), normal (5), or high (15). No migration discount."},
            {"dry_run", RPCArg::Type::BOOL, RPCArg::Default{false}, "Report the plan only."},
            {"broadcast", RPCArg::Type::BOOL, RPCArg::Default{false}, "Broadcast signed batches. Ignored when dry_run is true."},
        },
        RPCResult{RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::BOOL, "dry_run", "True when nothing was signed"},
                {RPCResult::Type::BOOL, "witness_active", "True when PQ witness is active at the next block"},
                {RPCResult::Type::NUM, "next_height", "Height of the next block"},
                {RPCResult::Type::NUM, "active_weight_cap", "Weight cap applied to this call"},
                {RPCResult::Type::NUM, "legacy_utxo_count", "Confirmed spendable legacy inputs selected"},
                {RPCResult::Type::STR_AMOUNT, "legacy_total", "Sum of those inputs"},
                {RPCResult::Type::NUM, "inputs_per_full_batch", "How many legacy inputs fit in active_weight_cap"},
                {RPCResult::Type::NUM, "estimated_tx_count", "Batches required for every selected input. Not a guaranteed block count."},
                {RPCResult::Type::NUM, "estimated_full_batch_weight", "Estimated weight of one full batch"},
                {RPCResult::Type::NUM, "uneconomic_utxo_count", "Inputs whose value cannot pay a 1-input fee"},
                {RPCResult::Type::STR, "destination_address", "Native destination used for the plan"},
                {RPCResult::Type::STR, "mempool_note", "How to submit the raw transaction to an upgraded node"},
                {RPCResult::Type::STR, "block_count_note", "Why estimated_tx_count is not a promised number of blocks"},
                {RPCResult::Type::STR, "unbroadcast_limit", "Present when a signed transaction was not broadcast"},
                {RPCResult::Type::NUM, "batches_built", "Number of transactions in this response"},
                {RPCResult::Type::ARR, "transactions", "Batches built by this call. Empty for dry_run.",
                    {
                        {RPCResult::Type::OBJ, "", "",
                            {
                                {RPCResult::Type::STR_HEX, "txid", "Transaction id when signed"},
                                {RPCResult::Type::STR_HEX, "hex", "Signed raw transaction"},
                                {RPCResult::Type::NUM, "weight", "Signed weight"},
                                {RPCResult::Type::NUM, "vsize", "Signed virtual size"},
                                {RPCResult::Type::STR_AMOUNT, "fee", "Fee"},
                                {RPCResult::Type::NUM, "inputs_used", "Input count"},
                                {RPCResult::Type::BOOL, "broadcast", "True if this call submitted the transaction"},
                                {RPCResult::Type::ARR, "inputs", "Prevouts spent by this batch",
                                    {
                                        {RPCResult::Type::OBJ, "", "",
                                            {
                                                {RPCResult::Type::STR_HEX, "txid", "Previous transaction id"},
                                                {RPCResult::Type::NUM, "vout", "Output index"},
                                            }},
                                    }},
                            }},
                    }},
            }},
        RPCExamples{
            HelpExampleCli("pqmigratelegacy", "\"all\" \"\" 10 1600000 \"normal\" true false") +
            HelpExampleCli("pqmigratelegacy", "\"all\" \"\" 1 1600000 \"normal\" false false")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            std::shared_ptr<CWallet> wallet = GetWalletForJSONRPCRequest(request);
            if (!wallet) {
                throw JSONRPCError(RPC_WALLET_NOT_FOUND, "No wallet selected");
            }
            LOCK(wallet->cs_wallet);

            const std::string source{request.params[0].get_str()};
            const bool all_legacy{source == "all"};
            CScript source_script;
            if (!all_legacy) {
                const CTxDestination decoded{DecodeDestination(source)};
                if (!std::holds_alternative<DilithiumPKHash>(decoded)) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "source_address must be a legacy Dilithium P2PKH address or \"all\"");
                }
                source_script = GetScriptForDestination(decoded);
                if (!IsPQPayToPubKeyHash(source_script)) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "source_address is not a legacy Dilithium P2PKH script");
                }
            }

            const int next_height{wallet->chain().getHeight().value_or(-1) + 1};
            const bool witness_active{Consensus::IsPQWitnessEnabled(Params().GetConsensus(), next_height)};
            const int active_cap{witness_active ? MIGRATION_MAX_STANDARD_WEIGHT : MAX_STANDARD_TX_WEIGHT};
            int requested_cap{active_cap};
            if (!request.params[3].isNull()) {
                requested_cap = request.params[3].getInt<int>();
            }
            if (requested_cap < 1000 || requested_cap > active_cap) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf(
                    "max_tx_weight must be between 1000 and %d (pq_witness_active=%d). The migration cap has no end height.",
                    active_cap, witness_active));
            }
            const int weight_cap{requested_cap};

            int max_batches{10};
            if (!request.params[2].isNull()) max_batches = request.params[2].getInt<int>();
            if (max_batches < 1 || max_batches > MIGRATION_BATCH_CAP) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("max_batches must be between 1 and %d", MIGRATION_BATCH_CAP));
            }
            const bool dry_run{request.params[5].isNull() ? false : request.params[5].get_bool()};
            bool broadcast{request.params[6].isNull() ? false : request.params[6].get_bool()};
            if (!dry_run) {
                EnsureWalletIsUnlocked(*wallet);
            }
            if (!dry_run && !broadcast) {
                max_batches = 1;
            }
            const std::string fee_policy{request.params[4].isNull() ? "normal" : request.params[4].get_str()};
            const CFeeRate feerate{MigrationFeeRate(fee_policy)};
            const int witness_scale{GetWitnessDiscountScale(Params().GetConsensus(), next_height)};

            CScript dest_script;
            std::string destination_address;
            if (request.params[1].isNull() || request.params[1].get_str().empty()) {
                if (dry_run) {
                    // Weight depends only on the 22-byte native program, not on the hash.
                    dest_script = CScript() << OP_2 << std::vector<unsigned char>(20, 0);
                } else {
                    const util::Result<CTxDestination> created{wallet->GetNewDestination(OutputType::DILITHIUM_BECH32, "")};
                    if (!created) {
                        throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(created).original);
                    }
                    dest_script = GetScriptForDestination(*created);
                    destination_address = EncodeDestination(*created);
                }
            } else {
                const CTxDestination destination{DecodeDestination(request.params[1].get_str())};
                if (!std::holds_alternative<DilithiumWitnessV0KeyHash>(destination)) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "destination_address must be a native PQ witness dil1 address");
                }
                dest_script = GetScriptForDestination(destination);
                destination_address = EncodeDestination(destination);
            }
            if (!IsNativePQWitnessProgram(dest_script)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "destination script is not a native PQ witness program");
            }

            std::vector<LegacyCoin> coins;
            CAmount total{0};
            int uneconomic{0};
            const PqSpendWeight one{EstimatePqSpendWeight(1, true, witness_scale, {dest_script})};
            const CAmount one_fee{feerate.GetFee(static_cast<uint32_t>(one.vsize))};
            for (const auto& [txid, wtx] : wallet->mapWallet) {
                const int depth{wallet->GetTxDepthInMainChain(wtx)};
                if (depth < 1) continue;
                if (wtx.IsCoinBase() && depth < COINBASE_MATURITY) continue;
                const CTransaction& tx{*wtx.tx};
                for (uint32_t i{0}; i < tx.vout.size(); ++i) {
                    const CTxOut& out{tx.vout[i]};
                    if (!IsPQPayToPubKeyHash(out.scriptPubKey)) continue;
                    if (!all_legacy && out.scriptPubKey != source_script) continue;
                    if (!(wallet->IsMine(out.scriptPubKey) & ISMINE_SPENDABLE)) continue;
                    const COutPoint outpoint{wtx.GetHash(), i};
                    if (wallet->IsSpent(outpoint)) continue;
                    std::vector<std::vector<unsigned char>> solutions;
                    if (Solver(out.scriptPubKey, solutions) != TxoutType::DILITHIUM_PUBKEYHASH || solutions.empty()) continue;
                    if (out.nValue <= one_fee) {
                        ++uneconomic;
                        continue;
                    }
                    LegacyCoin coin;
                    coin.outpoint = outpoint;
                    coin.value = out.nValue;
                    coin.script = out.scriptPubKey;
                    coin.keyid = CKeyID(uint160(solutions[0]));
                    coins.push_back(std::move(coin));
                    total += out.nValue;
                }
            }
            std::sort(coins.begin(), coins.end(), [](const LegacyCoin& a, const LegacyCoin& b) {
                return a.value > b.value;
            });

            const size_t per_batch{InputsThatFit(witness_scale, weight_cap, dest_script)};
            if (per_batch == 0) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "max_tx_weight cannot hold one legacy Dilithium input");
            }
            const int64_t estimated_txs{coins.empty() ? 0 : static_cast<int64_t>((coins.size() + per_batch - 1) / per_batch)};

            UniValue result{UniValue::VOBJ};
            result.pushKV("dry_run", dry_run);
            result.pushKV("witness_active", witness_active);
            result.pushKV("next_height", next_height);
            result.pushKV("active_weight_cap", weight_cap);
            result.pushKV("legacy_utxo_count", static_cast<int64_t>(coins.size()));
            result.pushKV("legacy_total", ValueFromAmount(total));
            result.pushKV("inputs_per_full_batch", static_cast<int64_t>(per_batch));
            result.pushKV("estimated_tx_count", estimated_txs);
            result.pushKV("estimated_full_batch_weight", EstimatePqSpendWeight(per_batch, true, witness_scale, {dest_script}).weight);
            result.pushKV("uneconomic_utxo_count", uneconomic);
            result.pushKV("destination_address", destination_address);
            result.pushKV("mempool_note", "Submit signed hex with sendrawtransaction on an upgraded node. Unupgraded peers reject the loose transaction as tx-size and will not relay it. A mined block remains valid for them.");
            result.pushKV("block_count_note", "estimated_tx_count is not a guaranteed block count; miners choose which transactions to include.");
            result.pushKV("unbroadcast_limit", (!broadcast && !dry_run)
                ? "One signed batch is returned and its inputs are not reserved. Broadcast the hex before calling pqmigratelegacy again."
                : "");

            UniValue txs{UniValue::VARR};
            if (!dry_run) {
                if (coins.empty()) {
                    throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "No confirmed spendable legacy Dilithium UTXO matched");
                }
                size_t offset{0};
                for (int batch{0}; batch < max_batches && offset < coins.size(); ++batch) {
                    const size_t count{std::min(per_batch, coins.size() - offset)};
                    const PqSpendWeight estimate{EstimatePqSpendWeight(count, true, witness_scale, {dest_script})};
                    const CAmount fee{feerate.GetFee(static_cast<uint32_t>(estimate.vsize))};
                    CAmount sum{0};
                    for (size_t i{0}; i < count; ++i) sum += coins[offset + i].value;
                    if (sum <= fee) break;
                    CMutableTransaction mtx;
                    mtx.version = 2;
                    for (size_t i{0}; i < count; ++i) {
                        mtx.vin.emplace_back(coins[offset + i].outpoint, CScript{}, PQ_SPEND_SEQUENCE);
                    }
                    mtx.vout.emplace_back(sum - fee, dest_script);
                    CTransaction view{mtx};
                    for (size_t i{0}; i < count; ++i) {
                        std::vector<unsigned char> priv;
                        std::vector<unsigned char> pub;
                        if (!LookupLegacyKey(*wallet, coins[offset + i].keyid, priv, pub)) {
                            throw JSONRPCError(RPC_WALLET_ERROR, "Missing Dilithium key for a selected legacy input");
                        }
                        const uint256 sighash{SignatureHash(coins[offset + i].script, view, i, SIGHASH_ALL, 0, SigVersion::BASE, nullptr)};
                        std::vector<unsigned char> msg(sighash.begin(), sighash.end());
                        std::vector<unsigned char> sig;
                        if (!PQ_Sign(sig, msg, priv)) {
                            throw JSONRPCError(RPC_WALLET_ERROR, "Dilithium signing failed");
                        }
                        sig.push_back(static_cast<unsigned char>(SIGHASH_ALL));
                        mtx.vin[i].scriptSig = CScript() << sig << pub;
                    }
                    CTransactionRef final_tx{MakeTransactionRef(CTransaction(mtx))};
                    const PqSpendWeight actual{MeasurePqSpendWeight(*final_tx, witness_scale)};
                    if (actual.weight > weight_cap) {
                        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Signed migration weight %d exceeds cap %d", actual.weight, weight_cap));
                    }
                    bool sent{false};
                    if (broadcast) {
                        std::string err;
                        if (!wallet->chain().broadcastTransaction(final_tx, MAX_MONEY, /*relay=*/true, err)) {
                            throw JSONRPCError(RPC_WALLET_ERROR, "Broadcast rejected: " + err);
                        }
                        wallet->AddToWallet(final_tx, TxStateInMempool{});
                        sent = true;
                    }
                    UniValue item{UniValue::VOBJ};
                    item.pushKV("txid", final_tx->GetHash().GetHex());
                    item.pushKV("hex", EncodeHexTx(*final_tx));
                    item.pushKV("weight", actual.weight);
                    item.pushKV("vsize", actual.vsize);
                    item.pushKV("fee", ValueFromAmount(fee));
                    item.pushKV("inputs_used", static_cast<int64_t>(count));
                    item.pushKV("broadcast", sent);
                    UniValue used{UniValue::VARR};
                    for (size_t i{0}; i < count; ++i) {
                        UniValue prev{UniValue::VOBJ};
                        prev.pushKV("txid", coins[offset + i].outpoint.hash.GetHex());
                        prev.pushKV("vout", static_cast<int64_t>(coins[offset + i].outpoint.n));
                        used.push_back(std::move(prev));
                    }
                    item.pushKV("inputs", used);
                    txs.push_back(std::move(item));
                    offset += count;
                }
            }
            result.pushKV("transactions", txs);
            result.pushKV("batches_built", static_cast<int64_t>(txs.size()));
            return result;
        },
    };
}

} // namespace wallet
