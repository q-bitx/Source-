// Copyright (c) 2026 The Q-BitX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <coins.h>
#include <consensus/amount.h>
#include <txmempool.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <consensus/validation.h>
#include <crypto/dilithium.h>
#include <crypto/dilithium_wrapper.h>
#include <node/miner.h>
#include <policy/policy.h>
#include <pow.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/pq_script.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <util/time.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

using node::BlockAssembler;

namespace {

struct PqKey {
    pqcrypto::dilithium::PQPublicKey pub{};
    pqcrypto::dilithium::PQPrivateKey priv{};
    std::vector<unsigned char> pubkey;
    CScript legacy;
    CScript native;

    PqKey()
    {
        pqcrypto::dilithium::keygen(&pub, &priv);
        pubkey.assign(pub.data, pub.data + DILITHIUM_PUBLICKEYBYTES);
        legacy = CScript() << OP_DUP << OP_HASH160 << ToByteVector(Hash160(pubkey)) << OP_EQUALVERIFY << OP_CHECKSIGDILITHIUM;
        native = CScript() << OP_2 << ToByteVector(Hash160(pubkey));
    }
};

void AdvanceClock(const Consensus::Params& params)
{
    SetMockTime(GetTime() + params.nPowTargetSpacing);
}

void Solve(CBlock& block, const Consensus::Params& params)
{
    uint64_t attempts{0};
    while (!CheckProofOfWork(block.GetHash(), block.nBits, params)) {
        BOOST_REQUIRE_MESSAGE(attempts < (1ULL << 20), "regtest proof-of-work search exceeded 1048576 attempts");
        ++block.nNonce;
        ++attempts;
    }
}

CBlock MineEmpty(node::NodeContext& node, const CScript& payout)
{
    const Consensus::Params& params{node.chainman->GetConsensus()};
    AdvanceClock(params);
    BlockAssembler::Options options;
    options.coinbase_output_script = payout;
    options.use_mempool = false;
    CBlock block{BlockAssembler{node.chainman->ActiveChainstate(), nullptr, options}.CreateNewBlock()->block};
    block.hashMerkleRoot = BlockMerkleRoot(block);
    Solve(block, params);
    std::shared_ptr<const CBlock> shared{std::make_shared<const CBlock>(block)};
    BOOST_REQUIRE(node.chainman->ProcessNewBlock(shared, true, true, nullptr));
    return block;
}

CMutableTransaction Spend(const PqKey& key, const std::vector<CTransactionRef>& inputs, const std::vector<CTxOut>& outputs)
{
    CMutableTransaction tx;
    tx.version = 2;
    tx.vin.resize(inputs.size());
    for (size_t i{0}; i < inputs.size(); ++i) {
        tx.vin[i].prevout = COutPoint(inputs[i]->GetHash(), 0);
    }
    tx.vout = outputs;
    for (size_t i{0}; i < inputs.size(); ++i) {
        const CAmount amount{inputs[i]->vout[0].nValue};
        const uint256 sighash{SignatureHash(key.legacy, CTransaction{tx}, i, SIGHASH_ALL, amount, SigVersion::BASE)};
        std::vector<unsigned char> msg(sighash.begin(), sighash.end());
        std::vector<unsigned char> sig;
        BOOST_REQUIRE(pqcrypto::dilithium::sign(&key.priv, msg.data(), msg.size(), sig));
        sig.push_back(static_cast<unsigned char>(SIGHASH_ALL));
        tx.vin[i].scriptSig = CScript() << sig << key.pubkey;
    }
    return tx;
}

std::string Submit(node::NodeContext& node, const CTransactionRef& tx)
{
    LOCK(cs_main);
    const MempoolAcceptResult result{node.chainman->ProcessTransaction(tx)};
    if (result.m_result_type == MempoolAcceptResult::ResultType::VALID) return {};
    return result.m_state.ToString();
}

CTxOut Pay(const CScript& script, CAmount value)
{
    return CTxOut{value, script};
}

struct MigrationSetup : public TestingSetup {
    PqKey key;
    std::vector<CTransactionRef> coinbases;
    static constexpr int kMine{210};

    MigrationSetup()
        : TestingSetup{ChainType::REGTEST}
    {
        for (int i{0}; i < kMine; ++i) {
            coinbases.push_back(MineEmpty(m_node, key.legacy).vtx[0]);
        }
        LOCK(cs_main);
        BOOST_REQUIRE_EQUAL(m_node.chainman->ActiveChain().Height(), kMine);
        BOOST_REQUIRE(Consensus::IsPQWitnessEnabled(m_node.chainman->GetConsensus(), kMine + 1));
    }

    std::vector<CTransactionRef> Take(int count)
    {
        BOOST_REQUIRE_GE(coinbases.size(), static_cast<size_t>(count));
        std::vector<CTransactionRef> out(coinbases.begin(), coinbases.begin() + count);
        coinbases.erase(coinbases.begin(), coinbases.begin() + count);
        return out;
    }
};

BOOST_FIXTURE_TEST_SUITE(migration_policy_tests, MigrationSetup)

BOOST_AUTO_TEST_CASE(permanent_policy_has_no_end_height)
{
    BOOST_CHECK_EQUAL(MAX_STANDARD_TX_WEIGHT, 400000);
    BOOST_CHECK_EQUAL(MIGRATION_MAX_STANDARD_WEIGHT, 1600000);
    BOOST_CHECK_EQUAL(QBX_MAX_BLOCK_WEIGHT, 8000000u);

    const auto inputs{Take(6)};
    CAmount total{0};
    for (const auto& in : inputs) total += in->vout[0].nValue;
    const CTransaction tx{Spend(key, inputs, {Pay(key.native, total - COIN)})};
    const int scale{GetWitnessDiscountScale(m_node.chainman->GetConsensus(), 270000)};
    const int64_t weight{GetTransactionWeightWithScale(tx, scale)};
    BOOST_CHECK_GT(weight, MAX_STANDARD_TX_WEIGHT);
    BOOST_CHECK_LE(weight, MIGRATION_MAX_STANDARD_WEIGHT);

    LOCK(cs_main);
    const CCoinsViewCache& view{m_node.chainman->ActiveChainstate().CoinsTip()};
    const Consensus::Params& params{m_node.chainman->GetConsensus()};
    BOOST_CHECK(!IsQualifyingLegacyMigration(tx, view, 9, params));
    for (const int height : {10, 269999, 270000, 270001, 1000000}) {
        BOOST_CHECK(IsQualifyingLegacyMigration(tx, view, height, params));
    }
}

BOOST_AUTO_TEST_CASE(mempool_accepts_only_a_qualifying_migration)
{
    const int scale{GetWitnessDiscountScale(m_node.chainman->GetConsensus(), 131)};

    {
        const auto inputs{Take(1)};
        const CTransactionRef legacy_small{MakeTransactionRef(Spend(key, inputs, {Pay(key.legacy, inputs[0]->vout[0].nValue - COIN)}))};
        BOOST_CHECK_LE(GetTransactionWeightWithScale(*legacy_small, scale), MAX_STANDARD_TX_WEIGHT);
        BOOST_CHECK_EQUAL(Submit(m_node, legacy_small), "");
    }
    CTransactionRef native_parent;
    {
        const auto inputs{Take(1)};
        native_parent = MakeTransactionRef(Spend(key, inputs, {Pay(key.native, inputs[0]->vout[0].nValue - COIN)}));
        BOOST_CHECK_LE(GetTransactionWeightWithScale(*native_parent, scale), MAX_STANDARD_TX_WEIGHT);
        BOOST_CHECK_EQUAL(Submit(m_node, native_parent), "");
    }
    {
        CMutableTransaction heavy;
        heavy.version = 2;
        heavy.vin.emplace_back(COutPoint(native_parent->GetHash(), 0));
        const CAmount each{100000};
        const CAmount budget{native_parent->vout[0].nValue - COIN};
        while (heavy.vout.size() < 2000 && static_cast<CAmount>(heavy.vout.size() + 1) * each <= budget) {
            heavy.vout.push_back(Pay(key.native, each));
            if (GetTransactionWeightWithScale(CTransaction{heavy}, scale) > MAX_STANDARD_TX_WEIGHT) break;
        }
        const int64_t heavy_weight{GetTransactionWeightWithScale(CTransaction{heavy}, scale)};
        BOOST_CHECK_GT(heavy_weight, MAX_STANDARD_TX_WEIGHT);
        BOOST_CHECK_LE(heavy_weight, MIGRATION_MAX_STANDARD_WEIGHT);
        const std::string reason{Submit(m_node, MakeTransactionRef(heavy))};
        BOOST_CHECK(reason.find("tx-size") != std::string::npos);
    }

    {
        const auto inputs{Take(6)};
        CAmount total{0};
        for (const auto& in : inputs) total += in->vout[0].nValue;
        const CTransactionRef legacy_out{MakeTransactionRef(Spend(key, inputs, {Pay(key.legacy, total - COIN)}))};
        BOOST_CHECK_GT(GetTransactionWeightWithScale(*legacy_out, scale), MAX_STANDARD_TX_WEIGHT);
        const std::string reason{Submit(m_node, legacy_out)};
        BOOST_CHECK_NE(reason, "");
        BOOST_CHECK(reason.find("tx-size") != std::string::npos);
    }

    const auto good_inputs{Take(6)};
    CAmount good_total{0};
    for (const auto& in : good_inputs) good_total += in->vout[0].nValue;
    const CTransactionRef good{MakeTransactionRef(Spend(key, good_inputs, {Pay(key.native, good_total - COIN)}))};
    const int64_t good_weight{GetTransactionWeightWithScale(*good, scale)};
    BOOST_CHECK_GT(good_weight, MAX_STANDARD_TX_WEIGHT);
    BOOST_CHECK_LE(good_weight, MIGRATION_MAX_STANDARD_WEIGHT);
    BOOST_CHECK_EQUAL(Submit(m_node, good), "");

    {
        auto extra{Take(5)};
        extra.push_back(good);
        CAmount total{0};
        for (const auto& in : extra) total += in->vout[0].nValue;
        const std::string reason{Submit(m_node, MakeTransactionRef(Spend(key, extra, {Pay(key.native, total - COIN)})))};
        BOOST_CHECK(reason.find("tx-size") != std::string::npos);
    }

    {
        const auto inputs{Take(6)};
        CAmount total{0};
        for (const auto& in : inputs) total += in->vout[0].nValue;
        CMutableTransaction mixed{Spend(key, inputs, {Pay(key.native, total - 2 * COIN), Pay(key.legacy, COIN)})};
        const std::string reason{Submit(m_node, MakeTransactionRef(mixed))};
        BOOST_CHECK(reason.find("tx-size") != std::string::npos);
    }
    {
        const auto inputs{Take(6)};
        CAmount total{0};
        for (const auto& in : inputs) total += in->vout[0].nValue;
        CMutableTransaction data{Spend(key, inputs, {Pay(key.native, total - COIN), Pay(CScript() << OP_RETURN << std::vector<unsigned char>{0x11, 0x22}, 0)})};
        const std::string reason{Submit(m_node, MakeTransactionRef(data))};
        BOOST_CHECK(reason.find("tx-size") != std::string::npos);
    }
    {
        const auto inputs{Take(6)};
        CAmount total{0};
        for (const auto& in : inputs) total += in->vout[0].nValue;
        CMutableTransaction witness{Spend(key, inputs, {Pay(key.native, total - COIN)})};
        witness.vin[0].scriptWitness.stack.push_back({0x01});
        const std::string reason{Submit(m_node, MakeTransactionRef(witness))};
        BOOST_CHECK_NE(reason, "");
    }
    {
        const auto inputs{Take(6)};
        CAmount total{0};
        for (const auto& in : inputs) total += in->vout[0].nValue;
        CMutableTransaction bad_version{Spend(key, inputs, {Pay(key.native, total - COIN)})};
        bad_version.version = 0;
        const std::string reason{Submit(m_node, MakeTransactionRef(bad_version))};
        BOOST_CHECK(reason.find("version") != std::string::npos);
    }
    {
        const auto inputs{Take(6)};
        CAmount total{0};
        for (const auto& in : inputs) total += in->vout[0].nValue;
        const CTransactionRef unpaid{MakeTransactionRef(Spend(key, inputs, {Pay(key.native, total)}))};
        const std::string reason{Submit(m_node, unpaid)};
        BOOST_CHECK(reason.find("min relay fee not met") != std::string::npos);
    }
    {
        auto inputs{Take(20)};
        CAmount total{0};
        for (const auto& in : inputs) total += in->vout[0].nValue;
        const CTransactionRef huge{MakeTransactionRef(Spend(key, inputs, {Pay(key.native, total - COIN)}))};
        BOOST_CHECK_GT(GetTransactionWeightWithScale(*huge, scale), MIGRATION_MAX_STANDARD_WEIGHT);
        const std::string reason{Submit(m_node, huge)};
        BOOST_CHECK(reason.find("tx-size") != std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(template_includes_two_migrations_inside_block_weight)
{
    std::vector<uint256> ids;
    for (int n{0}; n < 2; ++n) {
        const auto inputs{Take(6)};
        CAmount total{0};
        for (const auto& in : inputs) total += in->vout[0].nValue;
        const CTransactionRef tx{MakeTransactionRef(Spend(key, inputs, {Pay(key.native, total - COIN)}))};
        BOOST_CHECK_EQUAL(Submit(m_node, tx), "");
        ids.push_back(tx->GetHash());
    }
    BlockAssembler::Options options;
    options.coinbase_output_script = key.legacy;
    const auto tmpl{BlockAssembler{m_node.chainman->ActiveChainstate(), m_node.mempool.get(), options}.CreateNewBlock()};
    BOOST_REQUIRE(tmpl);
    int found{0};
    for (const uint256& id : ids) {
        for (const auto& tx : tmpl->block.vtx) {
            if (tx->GetHash() == id) ++found;
        }
    }
    BOOST_CHECK_EQUAL(found, 2);
    const int scale{GetWitnessDiscountScale(m_node.chainman->GetConsensus(), 131)};
    const int64_t weight{GetBlockWeightWithScale(tmpl->block, scale)};
    BOOST_CHECK_LE(weight, static_cast<int64_t>(Consensus::GetMaxBlockWeight(m_node.chainman->GetConsensus(), 131)));
    CBlock block{tmpl->block};
    block.hashMerkleRoot = BlockMerkleRoot(block);
    Solve(block, m_node.chainman->GetConsensus());
    std::shared_ptr<const CBlock> shared{std::make_shared<const CBlock>(block)};
    BOOST_REQUIRE(m_node.chainman->ProcessNewBlock(shared, true, true, nullptr));
}

BOOST_AUTO_TEST_CASE(eighteen_legacy_inputs_verify_without_dos_blowup)
{
    const auto one_in{Take(1)};
    const CTransaction one{Spend(key, one_in, {Pay(key.native, one_in[0]->vout[0].nValue - COIN)})};
    const auto many_in{Take(18)};
    CAmount total{0};
    for (const auto& in : many_in) total += in->vout[0].nValue;
    const CTransaction many{Spend(key, many_in, {Pay(key.native, total - COIN)})};
    const int scale{GetWitnessDiscountScale(m_node.chainman->GetConsensus(), 211)};
    const int64_t many_weight{GetTransactionWeightWithScale(many, scale)};
    BOOST_CHECK_GT(many_weight, MAX_STANDARD_TX_WEIGHT);
    BOOST_CHECK_LE(many_weight, MIGRATION_MAX_STANDARD_WEIGHT);
    BOOST_CHECK_EQUAL(many.vin.size(), 18u);

    const unsigned flags{STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_PQ_WITNESS};
    auto verify_all = [&](const CTransaction& tx, const std::vector<CTransactionRef>& inputs) {
        PrecomputedTransactionData txdata{tx};
        for (size_t i{0}; i < tx.vin.size(); ++i) {
            ScriptError err{SCRIPT_ERR_OK};
            const TransactionSignatureChecker checker{&tx, static_cast<unsigned int>(i), inputs[i]->vout[0].nValue, txdata, MissingDataBehavior::ASSERT_FAIL};
            BOOST_REQUIRE(VerifyScript(tx.vin[i].scriptSig, key.legacy, &tx.vin[i].scriptWitness, flags, checker, &err));
        }
    };

    const auto one_start{std::chrono::steady_clock::now()};
    verify_all(one, one_in);
    const auto one_ms{std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - one_start).count()};
    const auto many_start{std::chrono::steady_clock::now()};
    verify_all(many, many_in);
    const auto many_ms{std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - many_start).count()};
    BOOST_TEST_MESSAGE("dilithium verify 1 input " << one_ms << " ms; 18 inputs " << many_ms << " ms; weight " << many_weight);
    // Dilithium dominates. A quadratic blowup versus one input, or a multi-minute stall, fails.
    BOOST_CHECK_LT(many_ms, 30000);
    BOOST_CHECK_LT(many_ms, std::max<int64_t>(one_ms, 1) * 18 * 8 + 2000);

    const CTransactionRef accepted{MakeTransactionRef(many)};
    BOOST_CHECK_EQUAL(Submit(m_node, accepted), "");
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(migration_policy_scale_tests)

BOOST_AUTO_TEST_CASE(classify_thirty_five_thousand_scripts_stays_bounded)
{
    const std::vector<unsigned char> program(20, 0x11);
    const CScript legacy{CScript() << OP_DUP << OP_HASH160 << program << OP_EQUALVERIFY << OP_CHECKSIGDILITHIUM};
    const CScript native{CScript() << OP_2 << program};
    BOOST_REQUIRE(IsPQPayToPubKeyHash(legacy));
    BOOST_REQUIRE(IsNativePQWitnessProgram(native));

    constexpr int n{35000};
    std::vector<CAmount> values(n);
    const auto start{std::chrono::steady_clock::now()};
    int legacy_hits{0};
    int native_hits{0};
    for (int i{0}; i < n; ++i) {
        if (IsPQPayToPubKeyHash(legacy)) ++legacy_hits;
        if (IsNativePQWitnessProgram(native)) ++native_hits;
        values[i] = n - i;
    }
    std::sort(values.begin(), values.end(), std::greater<CAmount>());
    const auto ms{std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()};
    BOOST_CHECK_EQUAL(legacy_hits, n);
    BOOST_CHECK_EQUAL(native_hits, n);
    BOOST_CHECK_EQUAL(values.front(), n);
    BOOST_CHECK_EQUAL(values.back(), 1);
    BOOST_TEST_MESSAGE("classified and sorted " << n << " scripts in " << ms << " ms");
    BOOST_CHECK_LT(ms, 2000);
}

BOOST_AUTO_TEST_SUITE_END()

struct PermanenceSetup : public TestingSetup {
    PqKey key;
    std::vector<CTransactionRef> coinbases;

    PermanenceSetup()
        : TestingSetup{ChainType::REGTEST}
    {
        for (int i{0}; i < 120; ++i) {
            coinbases.push_back(MineEmpty(m_node, key.legacy).vtx[0]);
        }
    }

    std::vector<CTransactionRef> Take(int count)
    {
        BOOST_REQUIRE_GE(coinbases.size(), static_cast<size_t>(count));
        std::vector<CTransactionRef> out(coinbases.begin(), coinbases.begin() + count);
        coinbases.erase(coinbases.begin(), coinbases.begin() + count);
        return out;
    }
};

BOOST_FIXTURE_TEST_SUITE(migration_policy_permanence_tests, PermanenceSetup)

BOOST_AUTO_TEST_CASE(large_migration_stays_after_later_blocks)
{
    const auto small_inputs{Take(1)};
    const CTransactionRef small{MakeTransactionRef(Spend(key, small_inputs, {Pay(key.legacy, small_inputs[0]->vout[0].nValue - COIN)}))};
    BOOST_CHECK_LE(GetTransactionWeightWithScale(*small, PQ_WITNESS_SCALE_FACTOR), MAX_STANDARD_TX_WEIGHT);
    BOOST_CHECK_EQUAL(Submit(m_node, small), "");

    const auto inputs{Take(6)};
    CAmount total{0};
    for (const auto& in : inputs) total += in->vout[0].nValue;
    const CTransactionRef large{MakeTransactionRef(Spend(key, inputs, {Pay(key.native, total - COIN)}))};
    BOOST_CHECK_GT(GetTransactionWeightWithScale(*large, PQ_WITNESS_SCALE_FACTOR), MAX_STANDARD_TX_WEIGHT);
    BOOST_CHECK_EQUAL(Submit(m_node, large), "");

    for (int i{0}; i < 5; ++i) {
        MineEmpty(m_node, key.legacy);
    }
    BOOST_CHECK(m_node.mempool->exists(GenTxid::Txid(small->GetHash())));
    BOOST_CHECK(m_node.mempool->exists(GenTxid::Txid(large->GetHash())));
    const int next_height{WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Height()) + 1};
    BOOST_CHECK_GE(next_height, 126);
    LOCK(cs_main);
    BOOST_CHECK(IsQualifyingLegacyMigration(*large, m_node.chainman->ActiveChainstate().CoinsTip(), next_height, m_node.chainman->GetConsensus()));
    BOOST_CHECK(IsQualifyingLegacyMigration(*large, m_node.chainman->ActiveChainstate().CoinsTip(), 270000, m_node.chainman->GetConsensus()));
    BOOST_CHECK(IsQualifyingLegacyMigration(*large, m_node.chainman->ActiveChainstate().CoinsTip(), 270001, m_node.chainman->GetConsensus()));
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace
