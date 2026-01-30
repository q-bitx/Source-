#include <boost/test/unit_test.hpp>

#include <policy/policy.h>
#include <script/interpreter.h>

#include "consensus/amount.h"
#include "crypto/dilithium.h"
#include "crypto/dilithium_wrapper.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/standard.h"   // STANDARD_SCRIPT_VERIFY_FLAGS
#include "util/transaction_identifier.h" // Txid::FromUint256

BOOST_AUTO_TEST_SUITE(pq_verifyscript_tests)

static std::vector<unsigned char> ToVec(const uint8_t* p, size_t n)
{
    return std::vector<unsigned char>(p, p + n);
}

BOOST_AUTO_TEST_CASE(pq_checksighash_and_verifyscript_ok)
{
    pqcrypto::dilithium::PQPublicKey pub{};
    pqcrypto::dilithium::PQPrivateKey priv{};
    pqcrypto::dilithium::keygen(&pub, &priv);

    std::vector<unsigned char> vchPubKey = ToVec(pub.data, DILITHIUM_PUBLICKEYBYTES);

    // scriptPubKey: <pubkey> OP_PQCHECKSIG  (PUSHDATA!)
    CScript scriptPubKey = CScript() << vchPubKey << OP_PQCHECKSIG;

    CMutableTransaction tx;
    tx.version = 1;
    tx.vin.resize(1);
    tx.vout.resize(1);

    tx.vin[0].prevout = COutPoint(Txid::FromUint256(uint256::ZERO), 0);
    tx.vin[0].scriptSig = CScript();

    const CAmount amount = 1 * COIN;
    tx.vout[0].nValue = amount - 1000;
    tx.vout[0].scriptPubKey = CScript() << OP_TRUE;

    const int nHashType = SIGHASH_ALL;

    const CTransaction txConst(tx);

    uint256 sighash = SignatureHash(scriptPubKey, txConst, 0, nHashType, amount, SigVersion::BASE, nullptr);

    std::vector<unsigned char> msg(sighash.begin(), sighash.end());
    std::vector<unsigned char> sig;
    BOOST_REQUIRE(pqcrypto::dilithium::sign(&priv, msg.data(), msg.size(), sig));
    sig.push_back((unsigned char)nHashType);

    // scriptSig: <sig> (PUSHDATA!)
    tx.vin[0].scriptSig = CScript() << sig;

    ScriptError err = SCRIPT_ERR_OK;
    const unsigned int flags = STANDARD_SCRIPT_VERIFY_FLAGS;

    bool ok = VerifyScript(
    tx.vin[0].scriptSig,
    scriptPubKey,
    nullptr,
    flags,
    TransactionSignatureChecker(&txConst, 0, amount, MissingDataBehavior::ASSERT_FAIL),
    &err
);
    BOOST_CHECK_MESSAGE(ok, ScriptErrorString(err));
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
}

BOOST_AUTO_TEST_CASE(pq_bad_signature_fails)
{
    pqcrypto::dilithium::PQPublicKey pub{};
    pqcrypto::dilithium::PQPrivateKey priv{};
    pqcrypto::dilithium::keygen(&pub, &priv);

    std::vector<unsigned char> vchPubKey = ToVec(pub.data, DILITHIUM_PUBLICKEYBYTES);

    // scriptPubKey: <pubkey> OP_PQCHECKSIG
    CScript scriptPubKey = CScript() << vchPubKey << OP_PQCHECKSIG;

    CMutableTransaction tx;
    tx.version = 1;
    tx.vin.resize(1);
    tx.vout.resize(1);

    tx.vin[0].prevout = COutPoint(Txid::FromUint256(uint256::ZERO), 0);

    const CAmount amount = 1 * COIN;
    tx.vout[0].nValue = amount - 1000;
    tx.vout[0].scriptPubKey = CScript() << OP_TRUE;

    const CTransaction txConst(tx);

    uint256 sighash = SignatureHash(scriptPubKey, txConst, 0, SIGHASH_ALL, amount, SigVersion::BASE, nullptr);
    std::vector<unsigned char> msg(sighash.begin(), sighash.end());

    std::vector<unsigned char> sig;
    BOOST_REQUIRE(pqcrypto::dilithium::sign(&priv, msg.data(), msg.size(), sig));
    sig.push_back((unsigned char)SIGHASH_ALL);

    sig[0] ^= 0x01;

    tx.vin[0].scriptSig = CScript() << sig;

    ScriptError err = SCRIPT_ERR_OK;
    const unsigned int flags = STANDARD_SCRIPT_VERIFY_FLAGS;

bool ok = VerifyScript(
    tx.vin[0].scriptSig,
    scriptPubKey,
    nullptr,
    flags,
    TransactionSignatureChecker(&txConst, 0, amount, MissingDataBehavior::ASSERT_FAIL),
    &err
);

    BOOST_CHECK(!ok);
    BOOST_CHECK(err != SCRIPT_ERR_OK);
}

BOOST_AUTO_TEST_CASE(pq_wrong_pubkey_size_fails_with_pubkeytype)
{
    pqcrypto::dilithium::PQPublicKey pub{};
    pqcrypto::dilithium::PQPrivateKey priv{};
    pqcrypto::dilithium::keygen(&pub, &priv);

    // pubkey intentionally wrong size
    std::vector<unsigned char> badPub(pub.data, pub.data + DILITHIUM_PUBLICKEYBYTES - 1);

    // <badpub> OP_PQCHECKSIG
    CScript scriptPubKey = CScript() << badPub << OP_PQCHECKSIG;

    CMutableTransaction tx;
    tx.version = 1;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vin[0].prevout = COutPoint(Txid::FromUint256(uint256::ZERO), 0);

    const CAmount amount = 1 * COIN;
    tx.vout[0].nValue = amount - 1000;
    tx.vout[0].scriptPubKey = CScript() << OP_TRUE;

    const CTransaction txConst(tx);

    // signature content doesn't matter; it should fail earlier on pubkey size
    uint256 sighash = SignatureHash(scriptPubKey, txConst, 0, SIGHASH_ALL, amount, SigVersion::BASE, nullptr);
    std::vector<unsigned char> msg(sighash.begin(), sighash.end());
    std::vector<unsigned char> sig;
    BOOST_REQUIRE(pqcrypto::dilithium::sign(&priv, msg.data(), msg.size(), sig));
    sig.push_back((unsigned char)SIGHASH_ALL);

    // scriptSig pushes only signature (pubkey is in scriptPubKey)
    tx.vin[0].scriptSig = CScript() << sig;

    ScriptError err = SCRIPT_ERR_OK;
    const unsigned int flags = STANDARD_SCRIPT_VERIFY_FLAGS;

    bool ok = VerifyScript(
        tx.vin[0].scriptSig,
        scriptPubKey,
        nullptr,
        flags,
        TransactionSignatureChecker(&txConst, 0, amount, MissingDataBehavior::ASSERT_FAIL),
        &err
    );

    BOOST_CHECK(!ok);
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_PUBKEYTYPE);
}

BOOST_AUTO_TEST_CASE(pq_wrong_sig_size_fails_with_sig_der)
{
    pqcrypto::dilithium::PQPublicKey pub{};
    pqcrypto::dilithium::PQPrivateKey priv{};
    pqcrypto::dilithium::keygen(&pub, &priv);

    std::vector<unsigned char> vchPubKey = ToVec(pub.data, DILITHIUM_PUBLICKEYBYTES);

    // <pubkey> OP_PQCHECKSIG
    CScript scriptPubKey = CScript() << vchPubKey << OP_PQCHECKSIG;

    CMutableTransaction tx;
    tx.version = 1;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vin[0].prevout = COutPoint(Txid::FromUint256(uint256::ZERO), 0);

    const CAmount amount = 1 * COIN;
    tx.vout[0].nValue = amount - 1000;
    tx.vout[0].scriptPubKey = CScript() << OP_TRUE;

    const CTransaction txConst(tx);

    uint256 sighash = SignatureHash(scriptPubKey, txConst, 0, SIGHASH_ALL, amount, SigVersion::BASE, nullptr);
    std::vector<unsigned char> msg(sighash.begin(), sighash.end());
    std::vector<unsigned char> sig;
    BOOST_REQUIRE(pqcrypto::dilithium::sign(&priv, msg.data(), msg.size(), sig));

    // Make signature wrong length: drop one byte, then add hashtype
    if (!sig.empty()) sig.pop_back();
    sig.push_back((unsigned char)SIGHASH_ALL);

    tx.vin[0].scriptSig = CScript() << sig;

    ScriptError err = SCRIPT_ERR_OK;
    const unsigned int flags = STANDARD_SCRIPT_VERIFY_FLAGS;

    bool ok = VerifyScript(
        tx.vin[0].scriptSig,
        scriptPubKey,
        nullptr,
        flags,
        TransactionSignatureChecker(&txConst, 0, amount, MissingDataBehavior::ASSERT_FAIL),
        &err
    );

    BOOST_CHECK(!ok);
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_SIG_DER);
}


BOOST_AUTO_TEST_SUITE_END()
