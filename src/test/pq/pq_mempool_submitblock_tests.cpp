// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <consensus/amount.h>
#include <crypto/dilithium.h>
#include <crypto/dilithium_wrapper.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <txmempool.h>
#include <validation.h>

#include <vector>

BOOST_AUTO_TEST_SUITE(pq_mempool_submitblock_tests)

BOOST_FIXTURE_TEST_CASE(pq_mempool_and_block_accepts_valid_spend, TestChain100Setup)
{
    // Generate Dilithium keypair
    pqcrypto::dilithium::PQPublicKey pub{};
    pqcrypto::dilithium::PQPrivateKey priv{};
    pqcrypto::dilithium::keygen(&pub, &priv);

    std::vector<unsigned char> pk(pub.data, pub.data + DILITHIUM_PUBLICKEYBYTES);

    // scriptPubKey: <pubkey> OP_PQCHECKSIG
    CScript scriptPubKey = CScript() << pk << OP_PQCHECKSIG;

    const auto ToMemPool = [this](const CMutableTransaction& tx) {
        LOCK(cs_main);
        const MempoolAcceptResult result = m_node.chainman->ProcessTransaction(MakeTransactionRef(tx));
        return result.m_result_type == MempoolAcceptResult::ResultType::VALID;
    };

    // Build spend transaction
    CMutableTransaction spend;
    spend.version = 1;
    spend.vin.resize(1);
    spend.vin[0].prevout = COutPoint(m_coinbase_txns[0]->GetHash(), 0);
    spend.vout.resize(1);
    spend.vout[0].nValue = 11*CENT;
    spend.vout[0].scriptPubKey = scriptPubKey; // Can reuse scriptPubKey or use OP_TRUE

    // Sign with Dilithium
    uint256 hash = SignatureHash(scriptPubKey, spend, 0, SIGHASH_ALL, 0, SigVersion::BASE);
    std::vector<unsigned char> msg(hash.begin(), hash.end());
    std::vector<unsigned char> sig;
    BOOST_REQUIRE(pqcrypto::dilithium::sign(&priv, msg.data(), msg.size(), sig));
    sig.push_back((unsigned char)SIGHASH_ALL);
    spend.vin[0].scriptSig = CScript() << sig;

    // Test 1: Valid transaction should be accepted to mempool
    BOOST_CHECK(ToMemPool(spend));

    // Test 2: Valid transaction should be accepted in a block
    CScript coinbaseScript = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    CBlock block = CreateAndProcessBlock({spend}, coinbaseScript);
    {
        LOCK(cs_main);
        BOOST_CHECK(m_node.chainman->ActiveChain().Tip()->GetBlockHash() == block.GetHash());
    }

    // Cleanup mempool (should be empty after block was mined)
    BOOST_CHECK_EQUAL(m_node.mempool->size(), 0U);
}

BOOST_FIXTURE_TEST_CASE(pq_rejects_bad_signature_in_mempool_and_block, TestChain100Setup)
{
    // Generate Dilithium keypair
    pqcrypto::dilithium::PQPublicKey pub{};
    pqcrypto::dilithium::PQPrivateKey priv{};
    pqcrypto::dilithium::keygen(&pub, &priv);

    std::vector<unsigned char> pk(pub.data, pub.data + DILITHIUM_PUBLICKEYBYTES);

    // scriptPubKey: <pubkey> OP_PQCHECKSIG
    CScript scriptPubKey = CScript() << pk << OP_PQCHECKSIG;

    const auto ToMemPool = [this](const CMutableTransaction& tx) {
        LOCK(cs_main);
        const MempoolAcceptResult result = m_node.chainman->ProcessTransaction(MakeTransactionRef(tx));
        return result.m_result_type == MempoolAcceptResult::ResultType::VALID;
    };

    // Build spend transaction
    CMutableTransaction spend;
    spend.version = 1;
    spend.vin.resize(1);
    spend.vin[0].prevout = COutPoint(m_coinbase_txns[0]->GetHash(), 0);
    spend.vout.resize(1);
    spend.vout[0].nValue = 11*CENT;
    spend.vout[0].scriptPubKey = scriptPubKey;

    // Sign with Dilithium
    uint256 hash = SignatureHash(scriptPubKey, spend, 0, SIGHASH_ALL, 0, SigVersion::BASE);
    std::vector<unsigned char> msg(hash.begin(), hash.end());
    std::vector<unsigned char> sig;
    BOOST_REQUIRE(pqcrypto::dilithium::sign(&priv, msg.data(), msg.size(), sig));
    sig.push_back((unsigned char)SIGHASH_ALL);
    spend.vin[0].scriptSig = CScript() << sig;

    // Clone and tamper with signature (flip a byte before hashtype)
    CMutableTransaction bad = spend;
    std::vector<unsigned char> badSig = sig;
    if (!badSig.empty() && badSig.size() > 1) {
        badSig[0] ^= 0x01; // Flip first byte of signature (before hashtype)
    }
    bad.vin[0].scriptSig = CScript() << badSig;

    // Test 1: Bad signature should be rejected from mempool
    BOOST_CHECK(!ToMemPool(bad));

    // Test 2: Block with bad signature should be rejected
    CScript coinbaseScript = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    CBlock block = CreateAndProcessBlock({bad}, coinbaseScript);
    {
        LOCK(cs_main);
        BOOST_CHECK(m_node.chainman->ActiveChain().Tip()->GetBlockHash() != block.GetHash());
    }

    // Mempool should be empty (invalid transaction was not added)
    BOOST_CHECK_EQUAL(m_node.mempool->size(), 0U);
}

BOOST_AUTO_TEST_SUITE_END()

