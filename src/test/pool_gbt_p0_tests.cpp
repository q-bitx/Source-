// Copyright (c) 2026 The Q-BitX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <chain.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <crypto/dilithium.h>
#include <crypto/dilithium_wrapper.h>
#include <node/miner.h>
#include <policy/policy.h>
#include <pow.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <serialize.h>
#include <sync.h>
#include <test/util/setup_common.h>
#include <txmempool.h>
#include <util/time.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <memory>
#include <vector>

using node::BlockAssembler;
using node::GBT_POOL_COINBASE_EXTRA_OUTPUT_BYTES;
using node::MAX_COINBASE_SCRIPTSIG_SIZE;

namespace {

struct PqSpendKey {
    pqcrypto::dilithium::PQPublicKey pub{};
    pqcrypto::dilithium::PQPrivateKey priv{};
    std::vector<unsigned char> pubkey;
    CScript script;

    PqSpendKey()
    {
        pqcrypto::dilithium::keygen(&pub, &priv);
        pubkey.assign(pub.data, pub.data + DILITHIUM_PUBLICKEYBYTES);
        script = CScript() << OP_DUP << OP_HASH160 << ToByteVector(Hash160(pubkey)) << OP_EQUALVERIFY << OP_CHECKSIGDILITHIUM;
    }
};

void AdvanceRegtestClock(const Consensus::Params& params)
{
    // One-second spacing hardens regtest LWMA (active from height 1, target 150s)
    // until a 32-bit nonce search never finishes. Space blocks by the target.
    SetMockTime(GetTime() + params.nPowTargetSpacing);
}

void SolveRegtestProofOfWork(CBlock& block, const Consensus::Params& params)
{
    constexpr uint64_t max_attempts{1ULL << 20};
    uint64_t attempts{0};
    while (!CheckProofOfWork(block.GetHash(), block.nBits, params)) {
        BOOST_REQUIRE_MESSAGE(attempts < max_attempts,
                              "regtest proof-of-work search exceeded 1048576 attempts");
        ++block.nNonce;
        ++attempts;
    }
}

CBlock MineEmptyBlock(node::NodeContext& node, const CScript& payout)
{
    const Consensus::Params& params{node.chainman->GetConsensus()};
    AdvanceRegtestClock(params);
    BlockAssembler::Options options;
    options.coinbase_output_script = payout;
    options.use_mempool = false;
    Chainstate& chainstate{node.chainman->ActiveChainstate()};
    CBlock block{BlockAssembler{chainstate, nullptr, options}.CreateNewBlock()->block};
    block.hashMerkleRoot = BlockMerkleRoot(block);
    SolveRegtestProofOfWork(block, params);
    std::shared_ptr<const CBlock> shared{std::make_shared<const CBlock>(block)};
    BOOST_REQUIRE(node.chainman->ProcessNewBlock(shared, /*force_processing=*/true, /*min_pow_checked=*/true, nullptr));
    LOCK(cs_main);
    BOOST_REQUIRE_EQUAL(node.chainman->ActiveChain().Tip()->GetBlockHash(), block.GetHash());
    return block;
}

void MineEmptyBlocks(node::NodeContext& node, const CScript& payout, int count, std::vector<CTransactionRef>* coinbases)
{
    for (int i{0}; i < count; ++i) {
        CBlock block{MineEmptyBlock(node, payout)};
        if (coinbases) coinbases->push_back(block.vtx[0]);
    }
}

CMutableTransaction SignPqSpend(const PqSpendKey& key, const CTransaction& coinbase, const std::vector<CTxOut>& outputs)
{
    CMutableTransaction tx;
    tx.version = 2;
    tx.vin.resize(1);
    tx.vin[0].prevout = COutPoint(coinbase.GetHash(), 0);
    tx.vout = outputs;
    const CAmount amount{coinbase.vout[0].nValue};
    const uint256 sighash{SignatureHash(key.script, CTransaction{tx}, 0, SIGHASH_ALL, amount, SigVersion::BASE)};
    std::vector<unsigned char> msg(sighash.begin(), sighash.end());
    std::vector<unsigned char> sig;
    BOOST_REQUIRE(pqcrypto::dilithium::sign(&key.priv, msg.data(), msg.size(), sig));
    sig.push_back(static_cast<unsigned char>(SIGHASH_ALL));
    tx.vin[0].scriptSig = CScript() << sig << key.pubkey;
    return tx;
}

void Accept(node::NodeContext& node, const CTransactionRef& tx)
{
    LOCK(cs_main);
    const MempoolAcceptResult result{node.chainman->ProcessTransaction(tx)};
    BOOST_REQUIRE_MESSAGE(result.m_result_type == MempoolAcceptResult::ResultType::VALID, result.m_state.ToString());
}

CScript PaddedCoinbaseScript(int height)
{
    CScript sig{CScript() << static_cast<int64_t>(height) << OP_0};
    BOOST_REQUIRE_LE(sig.size(), MAX_COINBASE_SCRIPTSIG_SIZE);
    sig.resize(MAX_COINBASE_SCRIPTSIG_SIZE);
    return sig;
}

CTxOut ExtraOutputOfReserveMargin()
{
    CScript script;
    while (true) {
        CTxOut probe;
        probe.nValue = 0;
        probe.scriptPubKey = script;
        const size_t bytes{::GetSerializeSize(probe)};
        if (bytes == GBT_POOL_COINBASE_EXTRA_OUTPUT_BYTES) return probe;
        BOOST_REQUIRE_LT(bytes, GBT_POOL_COINBASE_EXTRA_OUTPUT_BYTES);
        script << OP_RETURN;
    }
}

struct MatureRegtest : public TestingSetup {
    PqSpendKey key;
    std::vector<CTransactionRef> coinbases;

    MatureRegtest()
        : TestingSetup{ChainType::REGTEST, TestOpts{.extra_args = {"-acceptnonstdtxn=1"}}}
    {
        MineEmptyBlocks(m_node, key.script, 100, &coinbases);
        LOCK(cs_main);
        BOOST_REQUIRE_EQUAL(m_node.chainman->ActiveChain().Height(), 100);
        // Serialized-size enforcement (height 5) and PQ witness (height 10) are both behind us.
        BOOST_REQUIRE(Consensus::EnforcesBlockSerializedSizeLimit(m_node.chainman->GetConsensus(), 101));
        BOOST_REQUIRE(Consensus::IsPQWitnessEnabled(m_node.chainman->GetConsensus(), 101));
    }
};

struct PreActivationRegtest : public TestingSetup {
    PqSpendKey key;
    std::vector<CTransactionRef> coinbases;

    PreActivationRegtest()
        : TestingSetup{ChainType::REGTEST, TestOpts{.extra_args = {
            "-acceptnonstdtxn=1",
            "-testactivationheight=pq_sigops@110",
            "-testactivationheight=pq_witness@110",
        }}}
    {
        MineEmptyBlocks(m_node, key.script, 100, &coinbases);
        LOCK(cs_main);
        BOOST_REQUIRE_EQUAL(m_node.chainman->ActiveChain().Height(), 100);
        BOOST_REQUIRE_EQUAL(m_node.chainman->GetConsensus().nPQSigopsHeight, 110);
        BOOST_REQUIRE_EQUAL(m_node.chainman->GetConsensus().nPQWitnessHeight, 110);
        BOOST_REQUIRE(!Consensus::IsPQWitnessEnabled(m_node.chainman->GetConsensus(), 101));
    }
};

} // namespace

BOOST_AUTO_TEST_SUITE(pool_gbt_p0_serialization)

BOOST_AUTO_TEST_CASE(compactsize_252_to_253_and_old_coinbase_underestimates)
{
    BOOST_CHECK_EQUAL(GetSizeOfCompactSize(252), 1);
    BOOST_CHECK_EQUAL(GetSizeOfCompactSize(253), 3);
    BOOST_CHECK_EQUAL(GetSizeOfCompactSize(MAX_COINBASE_SCRIPTSIG_SIZE), 1);

    CMutableTransaction tiny;
    tiny.vin.resize(1);
    tiny.vin[0].prevout.SetNull();
    tiny.vin[0].scriptSig = CScript() << OP_0 << OP_0;
    tiny.vout.resize(1);
    tiny.vout[0].scriptPubKey = CScript() << OP_TRUE;
    const CTransactionRef tx{MakeTransactionRef(std::move(tiny))};
    auto bytes_for = [&](size_t count) {
        CBlock block;
        block.vtx.assign(count, tx);
        return ::GetSerializeSize(TX_WITH_WITNESS(block));
    };
    const size_t at_252{bytes_for(252)};
    const size_t at_253{bytes_for(253)};
    BOOST_CHECK_EQUAL(at_252, 80 + 1 + 252 * tx->GetTotalSize());
    BOOST_CHECK_EQUAL(at_253, 80 + 3 + 253 * tx->GetTotalSize());
    BOOST_CHECK_EQUAL(at_253 - at_252, tx->GetTotalSize() + 2);

    // The pre-fix reserve was the short BIP34 coinbase. A 100-byte scriptSig
    // plus one output of the documented margin is larger, and that difference
    // is exactly the bytes the old reserve failed to hold.
    CMutableTransaction old_cb;
    old_cb.vin.resize(1);
    old_cb.vin[0].prevout.SetNull();
    old_cb.vin[0].scriptSig = CScript() << static_cast<int64_t>(230000) << OP_0;
    old_cb.vout.resize(1);
    old_cb.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << std::vector<unsigned char>(20, 1) << OP_EQUALVERIFY << OP_CHECKSIGDILITHIUM;
    old_cb.vout.push_back(CTxOut{0, CScript() << OP_RETURN});
    old_cb.vin[0].scriptWitness.stack.assign(1, std::vector<unsigned char>(32, 0));
    CMutableTransaction pool_cb{old_cb};
    const size_t short_len{pool_cb.vin[0].scriptSig.size()};
    pool_cb.vin[0].scriptSig = PaddedCoinbaseScript(230000);
    pool_cb.vout.push_back(ExtraOutputOfReserveMargin());
    const size_t old_bytes{MakeTransactionRef(old_cb)->GetTotalSize()};
    const size_t pool_bytes{MakeTransactionRef(pool_cb)->GetTotalSize()};
    BOOST_CHECK_GT(pool_bytes, old_bytes);
    BOOST_CHECK_EQUAL(pool_bytes - old_bytes, (MAX_COINBASE_SCRIPTSIG_SIZE - short_len) + GBT_POOL_COINBASE_EXTRA_OUTPUT_BYTES);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(pool_gbt_p0_tests, MatureRegtest)

BOOST_AUTO_TEST_CASE(create_new_block_includes_mempool_tx_without_null_coinbase)
{
    const CTransaction& coinbase{*coinbases[0]};
    const CAmount fee{COIN / 100};
    CTxOut change;
    change.nValue = coinbase.vout[0].nValue - fee;
    change.scriptPubKey = key.script;
    const CTransactionRef spend{MakeTransactionRef(SignPqSpend(key, coinbase, {change}))};
    Accept(m_node, spend);

    BlockAssembler::Options options;
    options.coinbase_output_script = key.script;
    const auto first{BlockAssembler{m_node.chainman->ActiveChainstate(), m_node.mempool.get(), options}.CreateNewBlock()};
    const auto second{BlockAssembler{m_node.chainman->ActiveChainstate(), m_node.mempool.get(), options}.CreateNewBlock()};
    BOOST_REQUIRE(first);
    BOOST_REQUIRE(second);

    for (const auto& tmpl : {first.get(), second.get()}) {
        const CBlock& block{tmpl->block};
        BOOST_REQUIRE_GE(block.vtx.size(), 2U);
        for (const CTransactionRef& tx : block.vtx) {
            BOOST_REQUIRE(tx);
        }
        // v0.3.3 inserted a default (null) CTransactionRef at vtx[0] and then
        // serialized that partial block from TestPackage once the serialized-size
        // limit was active. Serializing the null ref dereferences it in
        // CTransaction::HasWitness. This call serialized a real coinbase.
        CTransactionRef null_ref;
        BOOST_CHECK(!null_ref);
        BOOST_CHECK(block.vtx[0]->IsCoinBase());
        BOOST_CHECK_EQUAL(block.vtx[1]->GetHash(), spend->GetHash());
        const size_t serialized{::GetSerializeSize(TX_WITH_WITNESS(block))};
        BOOST_CHECK_GT(serialized, 80U);
        BOOST_CHECK_LE(serialized, static_cast<size_t>(Consensus::GetMaxBlockSerializedSize(m_node.chainman->GetConsensus(), 101)));
    }
}

BOOST_AUTO_TEST_CASE(witness_bytes_and_pool_coinbase_stay_inside_serialized_cap)
{
    const CTransaction& coinbase{*coinbases[0]};
    CScript witness_script;
    for (int i{0}; i < 24; ++i) witness_script << OP_DROP;
    witness_script << OP_TRUE;
    const CScript witness_program{GetScriptForDestination(WitnessV0ScriptHash(witness_script))};

    constexpr int n_children{45};
    const CAmount each{COIN / 5};
    BOOST_REQUIRE_GT(coinbase.vout[0].nValue, each * n_children);
    std::vector<CTxOut> fan_outputs(n_children, CTxOut{each, witness_program});
    const CTransactionRef fan{MakeTransactionRef(SignPqSpend(key, coinbase, fan_outputs))};
    Accept(m_node, fan);
    // Confirm the fan-out so the children are not one descendant package.
    // Descendant count would otherwise reject them past the default limit of 25.
    {
        const Consensus::Params& params{m_node.chainman->GetConsensus()};
        AdvanceRegtestClock(params);
        BlockAssembler::Options confirm_options;
        confirm_options.coinbase_output_script = key.script;
        CBlock confirm{BlockAssembler{m_node.chainman->ActiveChainstate(), m_node.mempool.get(), confirm_options}.CreateNewBlock()->block};
        BOOST_REQUIRE_GE(confirm.vtx.size(), 2U);
        confirm.hashMerkleRoot = BlockMerkleRoot(confirm);
        SolveRegtestProofOfWork(confirm, params);
        std::shared_ptr<const CBlock> shared{std::make_shared<const CBlock>(confirm)};
        BOOST_REQUIRE(m_node.chainman->ProcessNewBlock(shared, true, true, nullptr));
    }

    std::vector<CTransactionRef> children;
    children.reserve(n_children);
    for (int i{0}; i < n_children; ++i) {
        CMutableTransaction child;
        child.version = 2;
        child.vin.resize(1);
        child.vin[0].prevout = COutPoint(fan->GetHash(), i);
        child.vin[0].scriptWitness.stack.resize(25);
        for (int item{0}; item < 24; ++item) {
            child.vin[0].scriptWitness.stack[item].assign(MAX_SCRIPT_ELEMENT_SIZE, static_cast<unsigned char>(0x11));
        }
        child.vin[0].scriptWitness.stack[24].assign(witness_script.begin(), witness_script.end());
        child.vout.resize(1);
        child.vout[0].nValue = each - 100000;
        child.vout[0].scriptPubKey = key.script;
        children.push_back(MakeTransactionRef(std::move(child)));
        Accept(m_node, children.back());
        BOOST_CHECK_GT(children.back()->GetTotalSize(),
                       static_cast<uint64_t>(GetVirtualTransactionSize(*children.back(), 0, 0, PQ_WITNESS_SCALE_FACTOR)));
    }

    BlockAssembler::Options options;
    options.coinbase_output_script = key.script;
    const auto tmpl{BlockAssembler{m_node.chainman->ActiveChainstate(), m_node.mempool.get(), options}.CreateNewBlock()};
    BOOST_REQUIRE(tmpl);
    CBlock block{tmpl->block};
    const int height{WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Height() + 1)};
    const unsigned int cap{Consensus::GetMaxBlockSerializedSize(m_node.chainman->GetConsensus(), height)};

    uint64_t sum_tx{0};
    bool saw_witness_child{false};
    size_t included_children{0};
    for (const CTransactionRef& tx : block.vtx) {
        BOOST_REQUIRE(tx);
        sum_tx += tx->GetTotalSize();
        if (!tx->IsCoinBase() && tx->HasWitness()) {
            saw_witness_child = true;
            ++included_children;
            BOOST_CHECK_GT(tx->GetTotalSize(),
                           static_cast<uint64_t>(GetVirtualTransactionSize(*tx, 0, 0, PQ_WITNESS_SCALE_FACTOR)));
        }
    }
    BOOST_CHECK(saw_witness_child);
    BOOST_CHECK_LT(included_children, static_cast<size_t>(n_children));
    const size_t node_serialized{::GetSerializeSize(TX_WITH_WITNESS(block))};
    BOOST_CHECK_EQUAL(node_serialized, 80 + GetSizeOfCompactSize(block.vtx.size()) + sum_tx);
    BOOST_CHECK_LE(node_serialized, static_cast<size_t>(cap));

    const size_t node_coinbase_bytes{block.vtx[0]->GetTotalSize()};
    CMutableTransaction pool{*block.vtx[0]};
    const size_t short_len{pool.vin[0].scriptSig.size()};
    pool.vin[0].scriptSig = PaddedCoinbaseScript(height);
    pool.vout.push_back(ExtraOutputOfReserveMargin());
    const CTransactionRef pool_tx{MakeTransactionRef(std::move(pool))};
    BOOST_CHECK_EQUAL(pool_tx->GetTotalSize() - node_coinbase_bytes,
                      (MAX_COINBASE_SCRIPTSIG_SIZE - short_len) + GBT_POOL_COINBASE_EXTRA_OUTPUT_BYTES);
    // Control: the previous reserve (the node coinbase itself) is smaller than
    // this pool coinbase, so filling up to that reserve would cross the cap.
    BOOST_CHECK_GT(pool_tx->GetTotalSize(), node_coinbase_bytes);

    block.vtx[0] = pool_tx;
    const size_t pool_serialized{::GetSerializeSize(TX_WITH_WITNESS(block))};
    BOOST_CHECK_LE(pool_serialized, static_cast<size_t>(cap));
    BOOST_CHECK_EQUAL(pool_serialized, node_serialized - node_coinbase_bytes + pool_tx->GetTotalSize());
    const int scale{GetWitnessDiscountScale(m_node.chainman->GetConsensus(), height)};
    const int64_t pool_weight{GetBlockWeightWithScale(block, scale)};
    BOOST_CHECK_LE(pool_weight, static_cast<int64_t>(Consensus::GetMaxBlockWeight(m_node.chainman->GetConsensus(), height)));
    BOOST_CHECK_EQUAL(pool_tx->vin[0].scriptSig.size(), static_cast<size_t>(MAX_COINBASE_SCRIPTSIG_SIZE));

    // Same packed template, coinbase past the 2048-byte reserve. The node must
    // reject it. CheckBlock/ContextualCheckBlock run before the block is connected.
    {
        CBlock over{block};
        CMutableTransaction over_cb{*over.vtx[0]};
        const size_t deficit{pool_serialized > static_cast<size_t>(cap) ? 1 : (static_cast<size_t>(cap) - pool_serialized + 1)};
        std::vector<unsigned char> raw(static_cast<size_t>(MAX_SCRIPT_SIZE), static_cast<unsigned char>(OP_TRUE));
        const CScript filler(raw.begin(), raw.end());
        const size_t per_output{::GetSerializeSize(CTxOut{0, filler})};
        BOOST_REQUIRE_GT(per_output, 0U);
        const size_t n_out{(deficit + per_output - 1) / per_output};
        for (size_t i{0}; i < n_out; ++i) {
            over_cb.vout.emplace_back(0, filler);
        }
        over.vtx[0] = MakeTransactionRef(std::move(over_cb));
        over.hashMerkleRoot = BlockMerkleRoot(over);
        const size_t over_serialized{::GetSerializeSize(TX_WITH_WITNESS(over))};
        BOOST_CHECK_GT(over_serialized, static_cast<size_t>(cap));
        const int64_t over_weight{GetBlockWeightWithScale(over, scale)};
        BOOST_CHECK_GT(over_weight, static_cast<int64_t>(Consensus::GetMaxBlockWeight(m_node.chainman->GetConsensus(), height)));
        SolveRegtestProofOfWork(over, m_node.chainman->GetConsensus());
        const auto tip_before{WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Tip()->GetBlockHash())};
        std::shared_ptr<const CBlock> over_shared{std::make_shared<const CBlock>(over)};
        BOOST_CHECK(!m_node.chainman->ProcessNewBlock(over_shared, /*force_processing=*/true, /*min_pow_checked=*/true, nullptr));
        LOCK(cs_main);
        BOOST_CHECK_EQUAL(m_node.chainman->ActiveChain().Tip()->GetBlockHash(), tip_before);
    }

    block.hashMerkleRoot = BlockMerkleRoot(block);
    SolveRegtestProofOfWork(block, m_node.chainman->GetConsensus());
    std::shared_ptr<const CBlock> accepted{std::make_shared<const CBlock>(block)};
    BOOST_REQUIRE(m_node.chainman->ProcessNewBlock(accepted, /*force_processing=*/true, /*min_pow_checked=*/true, nullptr));
    LOCK(cs_main);
    BOOST_CHECK_EQUAL(m_node.chainman->ActiveChain().Tip()->GetBlockHash(), block.GetHash());
}

BOOST_AUTO_TEST_CASE(mined_template_includes_mempool_transaction)
{
    const CTransaction& coinbase{*coinbases[0]};
    CTxOut change;
    change.nValue = coinbase.vout[0].nValue - (COIN / 100);
    change.scriptPubKey = key.script;
    const CTransactionRef spend{MakeTransactionRef(SignPqSpend(key, coinbase, {change}))};
    Accept(m_node, spend);

    BlockAssembler::Options options;
    options.coinbase_output_script = key.script;
    Chainstate& chainstate{m_node.chainman->ActiveChainstate()};
    const auto first{BlockAssembler{chainstate, m_node.mempool.get(), options}.CreateNewBlock()};
    const auto second{BlockAssembler{chainstate, m_node.mempool.get(), options}.CreateNewBlock()};
    BOOST_REQUIRE(first && second);
    BOOST_CHECK_EQUAL(first->block.vtx[1]->GetHash(), spend->GetHash());
    BOOST_CHECK_EQUAL(second->block.vtx[1]->GetHash(), spend->GetHash());

    CBlock block{second->block};
    block.hashMerkleRoot = BlockMerkleRoot(block);
    SolveRegtestProofOfWork(block, m_node.chainman->GetConsensus());
    std::shared_ptr<const CBlock> shared{std::make_shared<const CBlock>(block)};
    BOOST_REQUIRE(m_node.chainman->ProcessNewBlock(shared, true, true, nullptr));
    LOCK(cs_main);
    const CBlockIndex* tip{m_node.chainman->ActiveChain().Tip()};
    BOOST_REQUIRE_EQUAL(tip->GetBlockHash(), block.GetHash());
    BOOST_CHECK_EQUAL(m_node.mempool->size(), 0U);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(pool_gbt_p0_activation, PreActivationRegtest)

BOOST_AUTO_TEST_CASE(mempool_metadata_tracks_activation_without_restart)
{
    const CTransaction& coinbase{*coinbases[0]};
    CTxOut change;
    change.nValue = coinbase.vout[0].nValue - (COIN / 10);
    change.scriptPubKey = key.script;
    const CTransactionRef spend{MakeTransactionRef(SignPqSpend(key, coinbase, {change}))};
    Accept(m_node, spend);

    int32_t weight_before{0};
    int64_t sigops_before{0};
    int scale_before{0};
    {
        LOCK(m_node.mempool->cs);
        const CTxMemPoolEntry* entry{m_node.mempool->GetEntry(spend->GetHash())};
        BOOST_REQUIRE(entry);
        weight_before = entry->GetTxWeight();
        sigops_before = entry->GetSigOpCost();
        scale_before = entry->GetWitnessDiscountScale();
    }
    BOOST_CHECK_EQUAL(scale_before, WITNESS_SCALE_FACTOR);
    BOOST_CHECK_EQUAL(weight_before, GetTransactionWeightWithScale(*spend, WITNESS_SCALE_FACTOR));
    // CHECKSIGDILITHIUM in the output is not counted until PQ sigops activates.
    BOOST_CHECK_EQUAL(sigops_before, 0);

    // Tip 100 -> 109. Next-block height becomes 110, where both flags turn on.
    MineEmptyBlocks(m_node, key.script, 9, nullptr);
    {
        LOCK(cs_main);
        BOOST_REQUIRE_EQUAL(m_node.chainman->ActiveChain().Height(), 109);
        BOOST_REQUIRE(Consensus::IsPQWitnessEnabled(m_node.chainman->GetConsensus(), 110));
    }

    int32_t weight_after{0};
    int64_t sigops_after{0};
    int scale_after{0};
    {
        LOCK(m_node.mempool->cs);
        const CTxMemPoolEntry* entry{m_node.mempool->GetEntry(spend->GetHash())};
        BOOST_REQUIRE(entry);
        weight_after = entry->GetTxWeight();
        sigops_after = entry->GetSigOpCost();
        scale_after = entry->GetWitnessDiscountScale();
    }
    BOOST_CHECK_EQUAL(scale_after, PQ_WITNESS_SCALE_FACTOR);
    BOOST_CHECK_EQUAL(weight_after, GetTransactionWeightWithScale(*spend, PQ_WITNESS_SCALE_FACTOR));
    BOOST_CHECK_EQUAL(weight_after, weight_before * (PQ_WITNESS_SCALE_FACTOR / WITNESS_SCALE_FACTOR));
    BOOST_CHECK_EQUAL(sigops_after, WITNESS_SCALE_FACTOR);
    BOOST_CHECK_NE(weight_after, weight_before);
    BOOST_CHECK_GT(sigops_after, sigops_before);

    BlockAssembler::Options options;
    options.coinbase_output_script = key.script;
    const auto tmpl{BlockAssembler{m_node.chainman->ActiveChainstate(), m_node.mempool.get(), options}.CreateNewBlock()};
    BOOST_REQUIRE(tmpl);
    BOOST_REQUIRE_GE(tmpl->block.vtx.size(), 2U);
    BOOST_CHECK_EQUAL(tmpl->block.vtx[1]->GetHash(), spend->GetHash());
    const int64_t block_weight{GetBlockWeightWithScale(tmpl->block, PQ_WITNESS_SCALE_FACTOR)};
    const int64_t stale_block_weight{GetBlockWeightWithScale(tmpl->block, WITNESS_SCALE_FACTOR)};
    BOOST_CHECK_GT(block_weight, stale_block_weight);

    // Same accept path LoadMempool uses.
    {
        LOCK(cs_main);
        {
            LOCK(m_node.mempool->cs);
            m_node.mempool->removeRecursive(*spend, MemPoolRemovalReason::REORG);
        }
        const MempoolAcceptResult again{m_node.chainman->ProcessTransaction(spend)};
        BOOST_REQUIRE_MESSAGE(again.m_result_type == MempoolAcceptResult::ResultType::VALID, again.m_state.ToString());
    }
    {
        LOCK(m_node.mempool->cs);
        const CTxMemPoolEntry* entry{m_node.mempool->GetEntry(spend->GetHash())};
        BOOST_REQUIRE(entry);
        BOOST_CHECK_EQUAL(entry->GetWitnessDiscountScale(), PQ_WITNESS_SCALE_FACTOR);
        BOOST_CHECK_EQUAL(entry->GetTxWeight(), weight_after);
        BOOST_CHECK_EQUAL(entry->GetSigOpCost(), sigops_after);
    }
}

BOOST_AUTO_TEST_SUITE_END()
