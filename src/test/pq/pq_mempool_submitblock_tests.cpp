// Copyright (c) 2024 The Bitcoin Core developers
// Copyright (c) 2026 The Q-BitX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <consensus/amount.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <validation.h>

BOOST_AUTO_TEST_SUITE(pq_mempool_submitblock_tests)

BOOST_FIXTURE_TEST_CASE(pq_mempool_and_block_accepts_valid_spend, TestChain100Setup)
{
    const auto ToMemPool = [this](const CMutableTransaction& tx) {
        LOCK(cs_main);
        const MempoolAcceptResult result = m_node.chainman->ProcessTransaction(MakeTransactionRef(tx));
        return result.m_result_type == MempoolAcceptResult::ResultType::VALID;
    };

    // Spend the fixture Dilithium P2PKH coinbase. coinbaseKey is ECDSA and
    // cannot sign it; CreateValidMempoolTransaction uses the fixture key.
    CMutableTransaction spend = CreateValidMempoolTransaction(
        m_coinbase_txns[0], 0, 0, coinbaseKey, m_coinbase_script, 11 * CENT, false);
    BOOST_CHECK(ToMemPool(spend));

    CBlock block = CreateAndProcessBlock({spend}, m_coinbase_script);
    {
        LOCK(cs_main);
        BOOST_CHECK(m_node.chainman->ActiveChain().Tip()->GetBlockHash() == block.GetHash());
    }
    BOOST_CHECK_EQUAL(m_node.mempool->size(), 0U);
}

BOOST_FIXTURE_TEST_CASE(pq_rejects_bad_signature_in_mempool_and_block, TestChain100Setup)
{
    const auto ToMemPool = [this](const CMutableTransaction& tx) {
        LOCK(cs_main);
        const MempoolAcceptResult result = m_node.chainman->ProcessTransaction(MakeTransactionRef(tx));
        return result.m_result_type == MempoolAcceptResult::ResultType::VALID;
    };

    CMutableTransaction spend = CreateValidMempoolTransaction(
        m_coinbase_txns[0], 0, 0, coinbaseKey, m_coinbase_script, 11 * CENT, false);
    CMutableTransaction bad = spend;
    BOOST_REQUIRE(bad.vin[0].scriptSig.size() > 4);
    bad.vin[0].scriptSig[4] ^= 0x01;

    BOOST_CHECK(!ToMemPool(bad));

    const uint256 tip_before = WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Tip()->GetBlockHash());
    CBlock block = CreateAndProcessBlock({bad}, m_coinbase_script);
    {
        LOCK(cs_main);
        BOOST_CHECK(m_node.chainman->ActiveChain().Tip()->GetBlockHash() == tip_before);
        BOOST_CHECK(m_node.chainman->ActiveChain().Tip()->GetBlockHash() != block.GetHash());
    }
    BOOST_CHECK_EQUAL(m_node.mempool->size(), 0U);
}

BOOST_AUTO_TEST_SUITE_END()
