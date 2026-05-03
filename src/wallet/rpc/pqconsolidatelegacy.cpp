// Copyright (c) 2026 The Q-BitX Core developers
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
#include <outputtype.h>
#include <policy/feerate.h>
#include <primitives/transaction.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <script/interpreter.h>
#include <script/solver.h>
#include <univalue.h>
#include <util/moneystr.h>
#include <util/result.h>
#include <util/strencodings.h>
#include <wallet/rpc/util.h>
#include <wallet/wallet.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace wallet {

namespace {

/** Hard cap per batch (aligned with pqsendtoaddress batching constant). */
static constexpr size_t LEGACY_PQ_CONSOLIDATE_MAX_INPUTS_CAP = 16;
/** Prevent unbounded mempool bursts from a single RPC. */
static constexpr int LEGACY_PQ_CONSOLIDATE_MAX_BATCHES_CAP = 32;

constexpr uint32_t SEQ_NONFINAL = 0xFFFFFFFE;

static bool IsPQScript(const CScript& script)
{
    std::vector<std::vector<unsigned char>> solutions;
    const TxoutType whichType = Solver(script, solutions);
    return whichType == TxoutType::DILITHIUM_PUBKEY ||
           whichType == TxoutType::DILITHIUM_PUBKEYHASH ||
           whichType == TxoutType::DILITHIUM_SCRIPTHASH ||
           whichType == TxoutType::DILITHIUM_WITNESS_V0_KEYHASH ||
           whichType == TxoutType::DILITHIUM_WITNESS_V0_SCRIPTHASH ||
           whichType == TxoutType::DILITHIUM_MULTISIG ||
           whichType == TxoutType::QBITX_DILITHIUM;
}

static CFeeRate FeeRateFromLevel(const std::string& fee_level)
{
    CAmount sat_per_kvb;
    if (fee_level == "low" || fee_level.empty()) {
        sat_per_kvb = 1 * 1000;
    } else if (fee_level == "normal") {
        sat_per_kvb = 5 * 1000;
    } else if (fee_level == "high") {
        sat_per_kvb = 15 * 1000;
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid fee_level. Must be 'low', 'normal', or 'high'");
    }
    return CFeeRate(sat_per_kvb);
}

/** Same sizing model as pqsendfrom / pqsendtoaddress (Dilithium3 PUSHDATA2 pushes). */
static int64_t LegacyPqTxVsizeEstimate(size_t num_inputs, size_t num_outputs)
{
    int64_t base_size = 10;
    if (num_inputs > 9) {
        base_size += 1;
    }
    if (num_outputs > 9) {
        base_size += 1;
    }

    const int64_t sig_size = static_cast<int64_t>(DILITHIUM_SIGNATUREBYTES) + 1;
    const int64_t pubkey_size = static_cast<int64_t>(DILITHIUM_PUBLICKEYBYTES);
    constexpr int64_t push_overhead = 3;
    const int64_t scriptsig_size = push_overhead + sig_size + push_overhead + pubkey_size;
    const int64_t scriptsig_varint = GetSizeOfCompactSize(scriptsig_size);
    const int64_t per_input = 36 + 4 + scriptsig_varint + scriptsig_size;

    const int64_t p2pkh_output = 8 + 1 + 25;

    return base_size + static_cast<int64_t>(num_inputs) * per_input + static_cast<int64_t>(num_outputs) * p2pkh_output;
}

static bool ExtractDilithiumPubkeyFromSecretKey(const std::vector<unsigned char>& sk_bytes,
                                                 std::vector<unsigned char>& pk_bytes)
{
    pk_bytes.clear();
    if (sk_bytes.size() == DILITHIUM_SECRETKEYBYTES + DILITHIUM_PUBLICKEYBYTES) {
        pk_bytes.assign(sk_bytes.begin() + DILITHIUM_SECRETKEYBYTES, sk_bytes.end());
        return true;
    }
    return false;
}

static bool LoadDilithiumKeys(CWallet& wallet, const CKeyID& keyid,
                               std::vector<unsigned char>& privkeyBytes,
                               std::vector<unsigned char>& pubkeyBytes)
{
    for (ScriptPubKeyMan* spkm : wallet.GetAllScriptPubKeyMans()) {
        if (auto* desc_spkm = dynamic_cast<DescriptorScriptPubKeyMan*>(spkm)) {
            if (desc_spkm->GetDilithiumKeys(keyid, privkeyBytes, pubkeyBytes)) {
                return true;
            }
        }
    }
    if (wallet.map_dilithium_priv.count(keyid) > 0) {
        privkeyBytes = wallet.map_dilithium_priv[keyid];
        if (wallet.map_dilithium_pub.count(keyid) > 0) {
            pubkeyBytes = wallet.map_dilithium_pub[keyid];
        }
        return true;
    }
    return false;
}

static void EnsurePubkey(CWallet& wallet, const CKeyID& keyid, std::vector<unsigned char>& pubkeyBytes, const std::vector<unsigned char>& privkeyBytes)
{
    if (!pubkeyBytes.empty()) {
        return;
    }
    std::vector<unsigned char> extractedPk;
    if (ExtractDilithiumPubkeyFromSecretKey(privkeyBytes, extractedPk)) {
        pubkeyBytes = extractedPk;
    } else if (wallet.map_dilithium_pub.count(keyid) > 0) {
        pubkeyBytes = wallet.map_dilithium_pub[keyid];
    } else {
        std::unique_ptr<DatabaseBatch> db_batch = wallet.GetDatabase().MakeBatch();
        std::vector<unsigned char> db_pubkey;
        if (db_batch->Read(std::make_pair(DBKeys::DILITHIUM_PUBKEY, keyid), db_pubkey)) {
            pubkeyBytes = db_pubkey;
            wallet.map_dilithium_pub[keyid] = pubkeyBytes;
        } else {
            throw JSONRPCError(RPC_WALLET_ERROR, "Dilithium public key not found for legacy PQ address");
        }
    }
}

/** Outpoints spendable for this script; confirmed (depth>=1); coinbase matured. Sorted by amount ascending, then outpoint. */
static std::vector<std::pair<COutPoint, CAmount>> CollectSortedLegacyPqUtxos(CWallet& wallet, const CScript& from_spk)
{
    std::vector<std::pair<COutPoint, CAmount>> out;
    for (const auto& [unused_txid, wtx] : wallet.mapWallet) {
        (void)unused_txid;
        const int depth = wallet.GetTxDepthInMainChain(wtx);
        if (depth < 1) {
            continue;
        }
        const CTransaction& tx = *wtx.tx;
        for (uint32_t i = 0; i < tx.vout.size(); ++i) {
            const CTxOut& tout = tx.vout[i];
            if (tout.scriptPubKey != from_spk || !IsPQScript(tout.scriptPubKey)) {
                continue;
            }
            if (wtx.IsCoinBase() && depth < COINBASE_MATURITY) {
                continue;
            }
            const Txid wtx_txid = wtx.GetHash();
            const COutPoint outpoint(wtx_txid, i);
            bool spent = false;
            for (const auto& [s_tid, s_wtx] : wallet.mapWallet) {
                (void)s_tid;
                const CTransaction& stx = *s_wtx.tx;
                for (const auto& vin : stx.vin) {
                    if (vin.prevout == outpoint) {
                        spent = true;
                        break;
                    }
                }
                if (spent) {
                    break;
                }
            }
            if (!spent) {
                out.emplace_back(outpoint, tout.nValue);
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) {
            return a.second < b.second;
        }
        if (a.first.hash != b.first.hash) {
            return a.first.hash < b.first.hash;
        }
        return a.first.n < b.first.n;
    });
    return out;
}

static void SignLegacyPqInputs(CMutableTransaction& mtx,
                                const std::vector<std::pair<COutPoint, CAmount>>& inputs,
                                const CScript& from_spk,
                                const std::vector<unsigned char>& privkeyBytes,
                                const std::vector<unsigned char>& pubkeyBytes)
{
    CTransaction tx_const(mtx);
    constexpr int32_t n_hash_type = SIGHASH_ALL;
    for (size_t i = 0; i < mtx.vin.size(); ++i) {
        (void)inputs[i].second;
        const uint256 sighash = SignatureHash(from_spk, tx_const, i, n_hash_type, 0, SigVersion::BASE, nullptr);
        std::vector<unsigned char> msg(sighash.begin(), sighash.end());
        std::vector<unsigned char> signature;
        if (!PQ_Sign(signature, msg, privkeyBytes)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign Dilithium consolidation transaction");
        }
        std::vector<unsigned char> sig_ht = signature;
        sig_ht.push_back(static_cast<unsigned char>(n_hash_type));
        CScript script_sig;
        script_sig << sig_ht << pubkeyBytes;
        mtx.vin[i].scriptSig = script_sig;
        mtx.vin[i].scriptWitness.stack.clear();
    }
}

} // namespace

RPCHelpMan pqconsolidatelegacy()
{
    return RPCHelpMan{"pqconsolidatelegacy",
        "\nPre-PQ-witness helper: merge many small legacy Dilithium P2PKH (27...) UTXOs from one address into fewer larger legacy outputs.\n"
        "Each batch sends funds to a freshly generated wallet-owned legacy PQ address (never rdil1). This reduces UTXO count before PQ witness activation so migration is easier later.\n"
        "Intended before PQ witness activation; use low max_batches and run gradually.\n"
        "UTXOs are ordered by ascending amount, then outpoint (deterministic).\n"
        "With dry_run=true, no address is reserved from the keypool and no transaction is broadcast; each batch detail sets to_address to null and to_address_policy explains that the destination is created when dry_run=false.\n",
        {
            {"from_legacy_address", RPCArg::Type::STR, RPCArg::Optional::NO, "Wallet-owned legacy PQ address (Dilithium P2PKH / 27...)."},
            {"fee_level", RPCArg::Type::STR, RPCArg::Default{"low"}, "'low' (default), 'normal', or 'high' (same fee semantics as pqsendfrom / pqsendtoaddress)."},
            {"max_inputs", RPCArg::Type::NUM, RPCArg::Default{16}, "Maximum legacy inputs per consolidation transaction (clamped to " + std::to_string(LEGACY_PQ_CONSOLIDATE_MAX_INPUTS_CAP) + ")."},
            {"dry_run", RPCArg::Type::BOOL, RPCArg::Default{true}, "If true, plan only (no keypool address, no broadcast, no rawtx); if false, allocate a new legacy receive address per batch and broadcast."},
            {"max_batches", RPCArg::Type::NUM, RPCArg::Default{1}, "Maximum consolidation transactions in this call (clamped to " + std::to_string(LEGACY_PQ_CONSOLIDATE_MAX_BATCHES_CAP) + ")."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "mode", "legacy_pq_consolidation"},
                {RPCResult::Type::STR, "status", "ok or nothing_to_consolidate"},
                {RPCResult::Type::BOOL, "dry_run", "Whether this invocation broadcast"},
                {RPCResult::Type::STR, "from_address", "Source legacy PQ address"},
                {RPCResult::Type::NUM, "candidate_utxo_count", "Spendable legacy UTXOs at from_address"},
                {RPCResult::Type::NUM, "candidate_total", "Sum of candidate UTXO amounts"},
                {RPCResult::Type::NUM, "max_inputs", "Per-transaction input cap used"},
                {RPCResult::Type::NUM, "max_batches", "Per-call batch cap used"},
                {RPCResult::Type::NUM, "planned_batches", "Batches planned for this call (capped)"},
                {RPCResult::Type::NUM, "created_tx_count", "Transactions built (dry_run) or broadcast"},
                {RPCResult::Type::NUM, "total_input_amount", "Sum of inputs across created txs"},
                {RPCResult::Type::NUM, "total_fee", "Sum of fees"},
                {RPCResult::Type::NUM, "total_output_amount", "Sum of consolidation outputs"},
                {RPCResult::Type::ARR, "txids", "Broadcast transaction ids (empty if dry_run)",
                    {{RPCResult::Type::STR_HEX, "", "txid"}}},
                {RPCResult::Type::ARR, "details", "Per-batch breakdown",
                    {
                        {RPCResult::Type::OBJ, "", "",
                            {
                                {RPCResult::Type::NUM, "batch_index", "0-based batch index"},
                                {RPCResult::Type::NUM, "inputs_used", "Number of legacy inputs in this batch"},
                                {RPCResult::Type::NUM, "input_amount", "Sum of input values"},
                                {RPCResult::Type::NUM, "fee", "Fee for this batch"},
                                {RPCResult::Type::NUM, "output_amount", "Single consolidation output amount"},
                                {RPCResult::Type::STR, "to_address", true, "Fresh legacy PQ destination when broadcast; null on dry_run"},
                                {RPCResult::Type::STR, "to_address_policy", true, "Present on dry_run when to_address is null"},
                                {RPCResult::Type::STR, "txid", true, "Hex txid if broadcast; null when dry_run"},
                                {RPCResult::Type::STR_HEX, "rawtx", true, "Serialized transaction hex when broadcast"},
                            }},
                    }},
                {RPCResult::Type::ARR, "warnings", "Optional warnings (e.g. PQ witness already active)",
                    {{RPCResult::Type::STR, "", "warning text"}}},
            }},
        RPCExamples{
            "\nPlan consolidation (no broadcast)\n" + HelpExampleCli("pqconsolidatelegacy", "\"27...\" low 16 true 1") +
            "\nBroadcast one batch\n" + HelpExampleCli("pqconsolidatelegacy", "\"27...\" low 16 false 1")},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue {
            std::shared_ptr<CWallet> wallet = GetWalletForJSONRPCRequest(request);
            if (!wallet) {
                throw JSONRPCError(RPC_WALLET_NOT_FOUND, "No wallet selected. Use -rpcwallet=<walletname> to specify which wallet to use.");
            }

            LOCK(wallet->cs_wallet);

            // Positional / named (post-transform): [0] from_legacy_address, [1] fee_level,
            // [2] max_inputs, [3] dry_run, [4] max_batches
            const std::string from_str = request.params[0].get_str();
            CTxDestination from_dest = DecodeDestination(from_str);
            if (!IsValidDestination(from_dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Invalid from_legacy_address: %s", from_str));
            }
            const auto* from_pkhash = std::get_if<DilithiumPKHash>(&from_dest);
            if (!from_pkhash) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                    "from_legacy_address must be a legacy Dilithium P2PKH (27...) address, not native PQ witness (rdil1) or other types");
            }

            std::string fee_level = "low";
            if (request.params.size() > 1 && !request.params[1].isNull()) {
                fee_level = request.params[1].get_str();
            }
            const CFeeRate feerate = FeeRateFromLevel(fee_level);

            int max_inputs_req = 16;
            if (request.params.size() > 2 && !request.params[2].isNull()) {
                max_inputs_req = request.params[2].getInt<int>();
            }
            if (max_inputs_req < 1) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "max_inputs must be >= 1");
            }
            const size_t max_inputs = std::min(static_cast<size_t>(max_inputs_req), LEGACY_PQ_CONSOLIDATE_MAX_INPUTS_CAP);

            bool dry_run = true;
            if (request.params.size() > 3 && !request.params[3].isNull()) {
                dry_run = request.params[3].get_bool();
            }

            int max_batches_req = 1;
            if (request.params.size() > 4 && !request.params[4].isNull()) {
                max_batches_req = request.params[4].getInt<int>();
            }
            if (max_batches_req < 1) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "max_batches must be >= 1");
            }
            const int max_batches = std::min(max_batches_req, LEGACY_PQ_CONSOLIDATE_MAX_BATCHES_CAP);

            const CScript from_spk = GetScriptForDestination(from_dest);

            const CKeyID keyid{CKeyID(uint160(*from_pkhash))};
            std::vector<unsigned char> privkey_bytes;
            std::vector<unsigned char> pubkey_bytes;
            if (!LoadDilithiumKeys(*wallet, keyid, privkey_bytes, pubkey_bytes)) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Legacy PQ keys for from_legacy_address not found in this wallet");
            }
            EnsurePubkey(*wallet, keyid, pubkey_bytes, privkey_bytes);
            if (pubkey_bytes.size() != DILITHIUM_PUBLICKEYBYTES) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Invalid Dilithium public key size");
            }
            CDilithiumPubKey dil_pub(pubkey_bytes);
            if (!dil_pub.IsValid()) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Invalid Dilithium public key");
            }
            DilithiumPKHash derived(dil_pub);
            if (CKeyID(uint160(derived)) != keyid) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Public key does not match from_legacy_address");
            }

            UniValue warnings(UniValue::VARR);
            std::optional<int> tip_h = wallet->chain().getHeight();
            if (!tip_h.has_value()) {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to read chain height");
            }
            const Consensus::Params& consensus = Params().GetConsensus();
            const int next_h = *tip_h + 1;
            if (Consensus::IsPQWitnessEnabled(consensus, next_h)) {
                warnings.push_back(
                    "PQ witness is already active; this RPC is intended for pre-activation legacy consolidation. Use low max_inputs.");
            }

            std::vector<std::pair<COutPoint, CAmount>> candidates = CollectSortedLegacyPqUtxos(*wallet, from_spk);
            CAmount candidate_total = 0;
            for (const auto& e : candidates) {
                candidate_total += e.second;
            }

            UniValue ret(UniValue::VOBJ);
            ret.pushKV("mode", "legacy_pq_consolidation");
            ret.pushKV("dry_run", dry_run);
            ret.pushKV("from_address", EncodeDestination(from_dest));
            ret.pushKV("candidate_utxo_count", static_cast<int>(candidates.size()));
            ret.pushKV("candidate_total", ValueFromAmount(candidate_total));
            ret.pushKV("max_inputs", static_cast<int>(max_inputs));
            ret.pushKV("max_batches", max_batches);

            if (candidates.size() <= 1) {
                ret.pushKV("status", "nothing_to_consolidate");
                ret.pushKV("planned_batches", 0);
                ret.pushKV("created_tx_count", 0);
                ret.pushKV("total_input_amount", ValueFromAmount(0));
                ret.pushKV("total_fee", ValueFromAmount(0));
                ret.pushKV("total_output_amount", ValueFromAmount(0));
                ret.pushKV("txids", UniValue(UniValue::VARR));
                ret.pushKV("details", UniValue(UniValue::VARR));
                ret.pushKV("warnings", warnings);
                return ret;
            }

            const int full_batches = static_cast<int>((candidates.size() + max_inputs - 1) / max_inputs);
            const int planned_batches = std::min(max_batches, full_batches);
            ret.pushKV("planned_batches", planned_batches);

            UniValue txids_arr(UniValue::VARR);
            UniValue details_arr(UniValue::VARR);
            CAmount total_in_amt = 0;
            CAmount total_fee_amt = 0;
            CAmount total_out_amt = 0;
            int created = 0;

            static constexpr const char* DRY_RUN_TO_POLICY = "fresh_legacy_address_created_on_broadcast";

            auto append_detail = [&](int batch_index, size_t n_in, CAmount in_amt, CAmount fee_amt, CAmount out_amt,
                                     const std::string& txid_hex,
                                     const std::optional<std::string>& raw_hex,
                                     const std::optional<std::string>& to_addr_str) {
                UniValue d(UniValue::VOBJ);
                d.pushKV("batch_index", batch_index);
                d.pushKV("inputs_used", static_cast<int>(n_in));
                d.pushKV("input_amount", ValueFromAmount(in_amt));
                d.pushKV("fee", ValueFromAmount(fee_amt));
                d.pushKV("output_amount", ValueFromAmount(out_amt));
                if (to_addr_str.has_value()) {
                    d.pushKV("to_address", *to_addr_str);
                } else {
                    UniValue na;
                    na.setNull();
                    d.pushKV("to_address", na);
                    d.pushKV("to_address_policy", DRY_RUN_TO_POLICY);
                }
                if (txid_hex.empty()) {
                    UniValue null_txid;
                    null_txid.setNull();
                    d.pushKV("txid", null_txid);
                } else {
                    d.pushKV("txid", txid_hex);
                }
                if (raw_hex.has_value() && !raw_hex->empty()) {
                    d.pushKV("rawtx", *raw_hex);
                }
                details_arr.push_back(std::move(d));
            };

            if (dry_run) {
                size_t offset = 0;
                for (int b = 0; b < planned_batches; ++b) {
                    const size_t remain = candidates.size() - offset;
                    if (remain < 2) {
                        break;
                    }
                    const size_t n_take = std::min(max_inputs, remain);
                    std::vector<std::pair<COutPoint, CAmount>> batch;
                    batch.reserve(n_take);
                    CAmount in_sum = 0;
                    for (size_t j = 0; j < n_take; ++j) {
                        batch.push_back(candidates[offset + j]);
                        in_sum += candidates[offset + j].second;
                    }
                    offset += n_take;

                    const int64_t vsize = LegacyPqTxVsizeEstimate(batch.size(), 1);
                    const CAmount fee = feerate.GetFee(static_cast<uint32_t>(std::max<int64_t>(1, vsize)));
                    if (in_sum <= fee) {
                        throw JSONRPCError(RPC_WALLET_ERROR,
                            strprintf("Batch %d: input amount %s does not exceed fee %s; lower fee_level or increase inputs",
                                b, FormatMoney(in_sum), FormatMoney(fee)));
                    }
                    const CAmount out_amt = in_sum - fee;

                    total_in_amt += in_sum;
                    total_fee_amt += fee;
                    total_out_amt += out_amt;
                    ++created;
                    append_detail(b, batch.size(), in_sum, fee, out_amt, "", std::nullopt, std::nullopt);
                }
            } else {
                for (int b = 0; b < planned_batches; ++b) {
                    std::vector<std::pair<COutPoint, CAmount>> live = CollectSortedLegacyPqUtxos(*wallet, from_spk);
                    if (live.size() < 2) {
                        break;
                    }
                    const size_t n_take = std::min(max_inputs, live.size());
                    std::vector<std::pair<COutPoint, CAmount>> batch;
                    batch.reserve(n_take);
                    CAmount in_sum = 0;
                    for (size_t j = 0; j < n_take; ++j) {
                        batch.push_back(live[j]);
                        in_sum += live[j].second;
                    }

                    const int64_t vsize = LegacyPqTxVsizeEstimate(batch.size(), 1);
                    const CAmount fee = feerate.GetFee(static_cast<uint32_t>(std::max<int64_t>(1, vsize)));
                    if (in_sum <= fee) {
                        throw JSONRPCError(RPC_WALLET_ERROR,
                            strprintf("Batch %d: input amount %s does not exceed fee %s", b, FormatMoney(in_sum), FormatMoney(fee)));
                    }
                    const CAmount out_amt = in_sum - fee;

                    const auto fresh_res = wallet->GetNewDestination(OutputType::DILITHIUM_LEGACY, "");
                    if (!fresh_res) {
                        throw JSONRPCError(RPC_WALLET_ERROR,
                            strprintf("Cannot obtain new legacy PQ address: %s", util::ErrorString(fresh_res).original));
                    }
                    const CTxDestination fresh_dest = *fresh_res;
                    if (!std::holds_alternative<DilithiumPKHash>(fresh_dest)) {
                        throw JSONRPCError(RPC_WALLET_ERROR, "Internal error: new destination is not legacy Dilithium P2PKH");
                    }
                    const CScript to_spk = GetScriptForDestination(fresh_dest);

                    CMutableTransaction mtx;
                    for (const auto& e : batch) {
                        mtx.vin.emplace_back(e.first, CScript{}, SEQ_NONFINAL);
                    }
                    mtx.vout.emplace_back(out_amt, to_spk);
                    SignLegacyPqInputs(mtx, batch, from_spk, privkey_bytes, pubkey_bytes);
                    const CTransaction tx(mtx);

                    std::string err_string;
                    const bool ok = wallet->chain().broadcastTransaction(MakeTransactionRef(tx), MAX_MONEY, true, err_string);
                    if (!ok) {
                        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Batch %d: broadcast failed: %s", b, err_string));
                    }
                    total_in_amt += in_sum;
                    total_fee_amt += fee;
                    total_out_amt += out_amt;
                    ++created;
                    const std::string txid_hex = tx.GetHash().GetHex();
                    txids_arr.push_back(txid_hex);
                    append_detail(b, batch.size(), in_sum, fee, out_amt, txid_hex,
                        std::make_optional(EncodeHexTx(tx)),
                        std::make_optional(EncodeDestination(fresh_dest)));
                }
            }

            ret.pushKV("status", "ok");
            ret.pushKV("created_tx_count", created);
            ret.pushKV("total_input_amount", ValueFromAmount(total_in_amt));
            ret.pushKV("total_fee", ValueFromAmount(total_fee_amt));
            ret.pushKV("total_output_amount", ValueFromAmount(total_out_amt));
            ret.pushKV("txids", txids_arr);
            ret.pushKV("details", details_arr);
            ret.pushKV("warnings", warnings);
            return ret;
        },
    };
}

} // namespace wallet
