#include <boost/test/unit_test.hpp>

#include <script/interpreter.h>
#include <script/script.h>
#include <script/standard.h>
#include <primitives/transaction.h>
#include <uint256.h>
#include "script/script_error.h"

#include <crypto/dilithium.h>   // PQ_GenerateKeypair / PQ_Sign / sizes
#include <vector>

BOOST_AUTO_TEST_SUITE(pqchecksig_tests)

BOOST_AUTO_TEST_CASE(pqchecksig_sighash_roundtrip_base)
{
    std::vector<unsigned char> pk(DILITHIUM_PUBLICKEYBYTES);
    std::vector<unsigned char> sk(DILITHIUM_SECRETKEYBYTES);
    PQ_GenerateKeypair(pk, sk);

    CScript scriptCode;
    scriptCode << pk << OP_PQCHECKSIG;

    CMutableTransaction mtx;
    mtx.nVersion = 2;
    mtx.vin.resize(1);
    mtx.vout.resize(1);

    // dummy prevout
    mtx.vin[0].prevout.hash = uint256::ZERO;
    mtx.vin[0].prevout.n = 0;
    mtx.vin[0].scriptSig = CScript();
    mtx.vin[0].nSequence = 0xFFFFFFFF;

    // dummy output
    mtx.vout[0].nValue = 1000;
    mtx.vout[0].scriptPubKey = CScript() << OP_TRUE;

    mtx.nLockTime = 0;

    const CTransaction tx(mtx);
    const unsigned int nIn = 0;
    const CAmount amount = 0;
    const int nHashType = SIGHASH_ALL;

    uint256 sighash = SignatureHash(scriptCode, tx, nIn, nHashType, amount, SigVersion::BASE, nullptr);
    std::vector<unsigned char> msg(sighash.begin(), sighash.end());

    std::vector<unsigned char> sig = PQ_Sign(msg, sk);
    sig.push_back((unsigned char)nHashType);

    TransactionSignatureChecker checker(&tx, nIn, amount, MissingDataBehavior::ASSERT_FAIL);
    ScriptExecutionData execdata;
    ScriptError err = SCRIPT_ERR_OK;

    bool ok = checker.CheckPQSignature(sig, pk, scriptCode, SigVersion::BASE, execdata, &err);

    BOOST_CHECK_MESSAGE(ok, "CheckPQSignature failed");
    BOOST_CHECK(err == SCRIPT_ERR_OK);
}

BOOST_AUTO_TEST_CASE(pqchecksig_rejects_bad_hashtype)
{
    // minimal tx context (BASE => amount not required)
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    CTransaction tx(mtx);

    const unsigned int nIn = 0;
    const CAmount amount = 0;
    PrecomputedTransactionData txdata;
    GenericTransactionSignatureChecker<const CTransaction> checker(&tx, nIn, amount, txdata);

    // scriptCode required by API
    CScript scriptCode;
    scriptCode << OP_1;

    ScriptExecutionData execdata;
    ScriptError err = SCRIPT_ERR_OK;

    // Pubkey correct size
    std::vector<unsigned char> pk(DILITHIUM_PUBLICKEYBYTES, 1);

    // Sig correct size, but invalid hashtype
    std::vector<unsigned char> sig(DILITHIUM_SIGNATUREBYTES + 1, 2);
    sig.back() = 0x02; // invalid (MVP allows only 0x01)

    bool ok = checker.CheckPQSignature(sig, pk, scriptCode, SigVersion::BASE, execdata, &err);
    BOOST_CHECK(!ok);
    BOOST_CHECK(err == SCRIPT_ERR_SIG_HASHTYPE);
}

BOOST_AUTO_TEST_CASE(pqchecksig_cheap_rejects_all_zero_pub_or_sig)
{
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    CTransaction tx(mtx);

    const unsigned int nIn = 0;
    const CAmount amount = 0;
    PrecomputedTransactionData txdata;
    GenericTransactionSignatureChecker<const CTransaction> checker(&tx, nIn, amount, txdata);

    CScript scriptCode;
    scriptCode << OP_1;

    ScriptExecutionData execdata;

    // A) all-zero pubkey => cheap reject
    {
        ScriptError err = SCRIPT_ERR_OK;

        std::vector<unsigned char> pk(DILITHIUM_PUBLICKEYBYTES, 0x00);
        std::vector<unsigned char> sig(DILITHIUM_SIGNATUREBYTES + 1, 0x00);
        sig.back() = 0x01; // valid hashtype to isolate the reject cause

        bool ok = checker.CheckPQSignature(sig, pk, scriptCode, SigVersion::BASE, execdata, &err);
        BOOST_CHECK(!ok);
        BOOST_CHECK(err == SCRIPT_ERR_SIG_DER);
    }

    // B) all-zero signature body => cheap reject
    {
        ScriptError err = SCRIPT_ERR_OK;

        std::vector<unsigned char> pk(DILITHIUM_PUBLICKEYBYTES, 1);
        std::vector<unsigned char> sig(DILITHIUM_SIGNATUREBYTES + 1, 0x00);
        sig.back() = 0x01;

        bool ok = checker.CheckPQSignature(sig, pk, scriptCode, SigVersion::BASE, execdata, &err);
        BOOST_CHECK(!ok);
        BOOST_CHECK(err == SCRIPT_ERR_SIG_DER);
    }
}

BOOST_AUTO_TEST_SUITE_END()
