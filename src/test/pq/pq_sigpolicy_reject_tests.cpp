#include <boost/test/unit_test.hpp>

#include "script/interpreter.h"
#include "script/script_error.h"
#include "crypto/dilithium.h"
#include "primitives/transaction.h"
#include "script/script.h"

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
    GenericTransactionSignatureChecker<CTransaction> checker(
    &tx, nIn, amount, txdata, MissingDataBehavior::FAIL);

    CScript scriptCode;
    scriptCode << OP_1;

    ScriptExecutionData execdata;
    ScriptError err = SCRIPT_ERR_OK;

    std::vector<unsigned char> pk(DILITHIUM_PUBLICKEYBYTES, 1);

    std::vector<unsigned char> sig(DILITHIUM_SIGNATUREBYTES + 1, 2);
    sig.back() = 0x02; // invalid (MVP allows only 0x01)

    bool ok = checker.CheckPQSignature(sig, pk, scriptCode, SigVersion::BASE, execdata, &err);
    BOOST_CHECK(!ok);
    BOOST_CHECK(err == SCRIPT_ERR_SIG_HASHTYPE);
    // BOOST_CHECK(err == SCRIPT_ERR_SIG_DER);
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
    GenericTransactionSignatureChecker<CTransaction> checker(
    &tx, nIn, amount, txdata, MissingDataBehavior::FAIL);

    CScript scriptCode;
    scriptCode << OP_1;

    ScriptExecutionData execdata;

    // A) all-zero pubkey => cheap reject
    {
        ScriptError err = SCRIPT_ERR_OK;

        std::vector<unsigned char> pk(DILITHIUM_PUBLICKEYBYTES, 0x00);
        std::vector<unsigned char> sig(DILITHIUM_SIGNATUREBYTES + 1, 0x00);
        sig.back() = 0x01; // valid hashtype

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

// src/test/pq/pq_sigpolicy_reject_tests.cpp
/*
#include <boost/test/unit_test.hpp>

#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <script/opcodes.h>

#include <primitives/transaction.h>

#include <crypto/dilithium.h> // DILITHIUM_PUBLICKEYBYTES / DILITHIUM_SIGNATUREBYTES

BOOST_AUTO_TEST_SUITE(pq_sigpolicy_reject_tests)

// 1) Reject invalid hashtype BEFORE SignatureHash/PQ_Verify (cheap)
BOOST_AUTO_TEST_CASE(pqchecksig_rejects_bad_hashtype)
{
    // Dummy tx (it won't be used because we reject before SignatureHash)
    CMutableTransaction mtx;
    CTransaction tx(mtx);

    PrecomputedTransactionData txdata; // unused in this test path
    const unsigned int nIn = 0;
    const CAmount amount = 0;

    // IMPORTANT: constructor in your tree expects 5 args (with MissingDataBehavior)
    GenericTransactionSignatureChecker<const CTransaction> checker(
        &tx, nIn, amount, txdata, MissingDataBehavior::FAIL);

    CScript scriptCode;
    scriptCode << OP_1;

    ScriptExecutionData execdata;
    ScriptError err = SCRIPT_ERR_OK;

    // Valid-sized pubkey/signature, but invalid hash type (baseType = 0)
    std::vector<unsigned char> pk(DILITHIUM_PUBLICKEYBYTES, 1);

    std::vector<unsigned char> sig(DILITHIUM_SIGNATUREBYTES, 2);
    sig.push_back(0x00); // invalid: baseType == 0 => must fail with SIG_HASHTYPE

    bool ok = checker.CheckPQSignature(sig, pk, scriptCode, SigVersion::BASE, execdata, &err);
    BOOST_CHECK(!ok);
    BOOST_CHECK(err == SCRIPT_ERR_SIG_HASHTYPE);
}

// 2) Reject all-zero pubkey/sig BEFORE SignatureHash/PQ_Verify (cheap DoS hardening)
BOOST_AUTO_TEST_CASE(pqchecksig_cheap_rejects_all_zero_pub_or_sig)
{
    CMutableTransaction mtx;
    CTransaction tx(mtx);

    PrecomputedTransactionData txdata;
    const unsigned int nIn = 0;
    const CAmount amount = 0;

    GenericTransactionSignatureChecker<const CTransaction> checker(
        &tx, nIn, amount, txdata, MissingDataBehavior::FAIL);

    CScript scriptCode;
    scriptCode << OP_1;

    ScriptExecutionData execdata;
    ScriptError err = SCRIPT_ERR_OK;

    // all-zero pubkey (should cheap-reject)
    std::vector<unsigned char> pk_zero(DILITHIUM_PUBLICKEYBYTES, 0);

    std::vector<unsigned char> sig(DILITHIUM_SIGNATUREBYTES, 7);
    sig.push_back(SIGHASH_ALL); // valid hashtype

    bool ok = checker.CheckPQSignature(sig, pk_zero, scriptCode, SigVersion::BASE, execdata, &err);
    BOOST_CHECK(!ok);
    BOOST_CHECK(err == SCRIPT_ERR_PUBKEYTYPE);

    // all-zero signature too (also should cheap-reject)
    ScriptError err2 = SCRIPT_ERR_OK;
    std::vector<unsigned char> pk(DILITHIUM_PUBLICKEYBYTES, 9);

    std::vector<unsigned char> sig_zero(DILITHIUM_SIGNATUREBYTES, 0);
    sig_zero.push_back(SIGHASH_ALL);

    bool ok2 = checker.CheckPQSignature(sig_zero, pk, scriptCode, SigVersion::BASE, execdata, &err2);
    BOOST_CHECK(!ok2);
    BOOST_CHECK(err2 == SCRIPT_ERR_PUBKEYTYPE);
}

BOOST_AUTO_TEST_SUITE_END()
*/
