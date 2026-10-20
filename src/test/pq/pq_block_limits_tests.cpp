// Q-BitX height-gated block weight / serialized-size limits (P0 fix).
#include <boost/test/unit_test.hpp>

#include <consensus/consensus.h>
#include <consensus/params.h>
#include <consensus/validation.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>

namespace {

CBlock MakeMinimalBlock(size_t witness_payload_bytes)
{
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 0;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;

    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout = COutPoint(Txid::FromUint256(uint256::ONE), 0);
    tx.vin[0].scriptSig = CScript() << OP_0;
    tx.vin[0].scriptWitness.stack.push_back(std::vector<unsigned char>(witness_payload_bytes, 0xab));
    tx.vout.resize(1);
    tx.vout[0].nValue = 0;
    tx.vout[0].scriptPubKey = CScript() << OP_TRUE;

    CBlock block;
    block.vtx.push_back(MakeTransactionRef(coinbase));
    block.vtx.push_back(MakeTransactionRef(tx));
    return block;
}

} // namespace

BOOST_AUTO_TEST_SUITE(pq_block_limits_tests)

BOOST_AUTO_TEST_CASE(block_limits_upgrade_height_helpers)
{
    Consensus::Params p{};
    p.nBlockLimitsUpgradeHeight = 220000;
    p.nPQWitnessHeight = 230000;

    BOOST_CHECK(!Consensus::IsBlockLimitsUpgraded(p, 219999));
    BOOST_CHECK(Consensus::IsBlockLimitsUpgraded(p, 220000));
    BOOST_CHECK(Consensus::IsBlockLimitsUpgraded(p, 230000));

    BOOST_CHECK_EQUAL(Consensus::GetMaxBlockWeight(p, 219999), LEGACY_MAX_BLOCK_WEIGHT);
    BOOST_CHECK_EQUAL(Consensus::GetMaxBlockWeight(p, 220000), QBX_MAX_BLOCK_WEIGHT);
    BOOST_CHECK_EQUAL(Consensus::GetMaxBlockSerializedSize(p, 219999), LEGACY_MAX_BLOCK_P2P_SERIALIZED_SIZE);
    BOOST_CHECK_EQUAL(Consensus::GetMaxBlockSerializedSize(p, 220000), QBX_MAX_BLOCK_SERIALIZED_SIZE);

    BOOST_CHECK(!Consensus::EnforcesBlockSerializedSizeLimit(p, 219999));
    BOOST_CHECK(Consensus::EnforcesBlockSerializedSizeLimit(p, 220000));
}

BOOST_AUTO_TEST_CASE(post_upgrade_rejects_oversized_physical_block_below_legacy_weight)
{
    Consensus::Params p{};
    p.nBlockLimitsUpgradeHeight = 220000;
    p.nPQWitnessHeight = 230000;

    const int height = 230000; // k=16 active
    const int wscale = GetWitnessDiscountScale(p, height);

    // Large witness: physical size can exceed 8MB while weight stays under legacy 16M.
    const CBlock block = MakeMinimalBlock(8'500'000);
    const int64_t weight = GetBlockWeightWithScale(block, wscale);
    const size_t serialized = ::GetSerializeSize(TX_WITH_WITNESS(block));

    BOOST_CHECK(weight <= static_cast<int64_t>(LEGACY_MAX_BLOCK_WEIGHT));
    BOOST_CHECK(serialized > QBX_MAX_BLOCK_SERIALIZED_SIZE);
    BOOST_CHECK_GT(weight, static_cast<int64_t>(QBX_MAX_BLOCK_WEIGHT)); // also over 8M weight

    BOOST_CHECK(Consensus::EnforcesBlockSerializedSizeLimit(p, height));
    BOOST_CHECK_GT(serialized, Consensus::GetMaxBlockSerializedSize(p, height));
}

BOOST_AUTO_TEST_CASE(post_upgrade_accepts_block_under_8m_physical_and_weight)
{
    Consensus::Params p{};
    p.nBlockLimitsUpgradeHeight = 220000;
    p.nPQWitnessHeight = 230000;

    const int height = 225000; // upgraded, k=4 still
    const int wscale = GetWitnessDiscountScale(p, height);
    const CBlock block = MakeMinimalBlock(1000);

    const int64_t weight = GetBlockWeightWithScale(block, wscale);
    const size_t serialized = ::GetSerializeSize(TX_WITH_WITNESS(block));

    BOOST_CHECK_LE(weight, static_cast<int64_t>(QBX_MAX_BLOCK_WEIGHT));
    BOOST_CHECK_LE(serialized, static_cast<size_t>(QBX_MAX_BLOCK_SERIALIZED_SIZE));
}

BOOST_AUTO_TEST_CASE(pre_upgrade_no_serialized_consensus_cap)
{
    Consensus::Params p{};
    p.nBlockLimitsUpgradeHeight = 220000;

    BOOST_CHECK(!Consensus::EnforcesBlockSerializedSizeLimit(p, 100000));
    // P2P guidance still 4MB pre-upgrade
    BOOST_CHECK_EQUAL(Consensus::GetMaxBlockSerializedSize(p, 100000), LEGACY_MAX_BLOCK_P2P_SERIALIZED_SIZE);
}

BOOST_AUTO_TEST_CASE(block_limits_reorg_detection)
{
    Consensus::Params p{};
    p.nBlockLimitsUpgradeHeight = 220000;

    // Helper compares next-block state: tip height + 1.
    // At tip 219999, the next block is 220000, so upgraded limits already apply.
    BOOST_CHECK(!IsBlockLimitsUpgradeChangedAcrossTips(p, 219997, 219998));
    BOOST_CHECK(IsBlockLimitsUpgradeChangedAcrossTips(p, 219998, 219999));
    BOOST_CHECK(!IsBlockLimitsUpgradeChangedAcrossTips(p, 219999, 220000));
    BOOST_CHECK(!IsBlockLimitsUpgradeChangedAcrossTips(p, 220000, 220001));

    // Backward reorg across the same boundary should also trigger.
    BOOST_CHECK(IsBlockLimitsUpgradeChangedAcrossTips(p, 219999, 219998));
}

BOOST_AUTO_TEST_CASE(tx_count_compactsize_and_coinbase_scriptsig_width)
{
    // Lengths 2..100 never cross the CompactSize step at 253.
    BOOST_CHECK_EQUAL(GetSizeOfCompactSize(2), 1);
    BOOST_CHECK_EQUAL(GetSizeOfCompactSize(100), 1);
    BOOST_CHECK_EQUAL(GetSizeOfCompactSize(252), 1);
    BOOST_CHECK_EQUAL(GetSizeOfCompactSize(253), 3);

    CMutableTransaction tiny;
    tiny.vin.resize(1);
    tiny.vin[0].prevout.SetNull();
    tiny.vin[0].scriptSig = CScript() << OP_0 << OP_0;
    tiny.vout.resize(1);
    tiny.vout[0].scriptPubKey = CScript() << OP_TRUE;
    const CTransactionRef tx{MakeTransactionRef(tiny)};

    auto block_bytes = [&](size_t n) {
        CBlock block;
        block.vtx.assign(n, tx);
        return ::GetSerializeSize(TX_WITH_WITNESS(block));
    };
    const size_t at_252{block_bytes(252)};
    const size_t at_253{block_bytes(253)};
    BOOST_CHECK_EQUAL(at_252, 80 + GetSizeOfCompactSize(252) + 252 * tx->GetTotalSize());
    BOOST_CHECK_EQUAL(at_253, 80 + GetSizeOfCompactSize(253) + 253 * tx->GetTotalSize());
    // One extra transaction, plus the tx-count CompactSize growing from 1 byte to 3.
    BOOST_CHECK_EQUAL(at_253 - at_252, tx->GetTotalSize() + 2);

    CMutableTransaction short_cb;
    short_cb.vin.resize(1);
    short_cb.vin[0].prevout.SetNull();
    short_cb.vin[0].scriptSig = CScript() << static_cast<int64_t>(230000) << OP_0;
    short_cb.vout.resize(1);
    short_cb.vout[0].scriptPubKey = CScript() << OP_TRUE;
    CMutableTransaction max_cb = short_cb;
    max_cb.vin[0].scriptSig = short_cb.vin[0].scriptSig;
    max_cb.vin[0].scriptSig.resize(100);
    const size_t short_len{short_cb.vin[0].scriptSig.size()};
    BOOST_CHECK_LT(short_len, 100U);
    BOOST_CHECK_EQUAL(MakeTransactionRef(max_cb)->GetTotalSize() - MakeTransactionRef(short_cb)->GetTotalSize(),
                      100 - short_len);
}

BOOST_AUTO_TEST_SUITE_END()
