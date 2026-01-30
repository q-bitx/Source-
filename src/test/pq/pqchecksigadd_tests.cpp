#include <boost/test/unit_test.hpp>

#include <script/interpreter.h>
#include <script/script.h>
#include <script/opcodes.h>
#include <primitives/transaction.h>
#include <uint256.h>
#include "script/script_error.h"

#include <crypto/dilithium.h>
#include <vector>

BOOST_AUTO_TEST_SUITE(pqchecksigadd_tests)

BOOST_AUTO_TEST_CASE(pqchecksigadd_valid_sig_increments_counter)
{
    // Generate PQ keys
    std::vector<unsigned char> pk(DILITHIUM_PUBLICKEYBYTES);
    std::vector<unsigned char> sk(DILITHIUM_SECRETKEYBYTES);
    PQ_GenerateKeypair(pk, sk);

    // Build scriptPubKey
    CScript scriptCode;
    scriptCode << pk << OP_PQCHECKSIG;

    // Minimal transaction for sighash
    CMutableTransaction mtx;
    mtx.nVersion = 2;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    mtx.vin[0].prevout.hash = uint256::ZERO;
    mtx.vin[0].prevout.n = 0;
    mtx.vin[0].scriptSig = CScript();
    mtx.vin[0].nSequence = 0xFFFFFFFF;
    mtx.vout[0].nValue = 1000;
    mtx.vout[0].scriptPubKey = CScript() << OP_TRUE;
    mtx.nLockTime = 0;

    const CTransaction tx(mtx);
    const unsigned int nIn = 0;
    const CAmount amount = 0;
    const int nHashType = SIGHASH_ALL;

    // Compute sighash
    uint256 sighash = SignatureHash(scriptCode, tx, nIn, nHashType, amount, SigVersion::BASE, nullptr);
    std::vector<unsigned char> msg(sighash.begin(), sighash.end());

    // Sign
    std::vector<unsigned char> sig;
    PQ_Sign(sig, msg, sk);
    sig.push_back((unsigned char)nHashType);

    // Build script: [num=5, sig, pubkey, OP_PQCHECKSIGADD] -> should result in 6
    CScript script;
    script << CScriptNum(5).getvch() << sig << pk << OP_PQCHECKSIGADD;

    // Execute
    std::vector<std::vector<unsigned char>> stack;
    TransactionSignatureChecker checker(&tx, nIn, amount, MissingDataBehavior::ASSERT_FAIL);
    ScriptExecutionData execdata;
    ScriptError err = SCRIPT_ERR_OK;

    bool ok = EvalScript(stack, script, SCRIPT_VERIFY_NONE, checker, SigVersion::BASE, execdata, &err);

    BOOST_CHECK(ok);
    BOOST_CHECK(err == SCRIPT_ERR_OK);
    BOOST_CHECK(stack.size() == 1);

    // Verify counter was incremented
    CScriptNum result(stack.back(), false);
    BOOST_CHECK(result == 6);
}

BOOST_AUTO_TEST_CASE(pqchecksigadd_invalid_sig_leaves_counter_unchanged)
{
    // Generate PQ keys
    std::vector<unsigned char> pk(DILITHIUM_PUBLICKEYBYTES);
    std::vector<unsigned char> sk(DILITHIUM_SECRETKEYBYTES);
    PQ_GenerateKeypair(pk, sk);

    // Build scriptPubKey
    CScript scriptCode;
    scriptCode << pk << OP_PQCHECKSIG;

    // Minimal transaction
    CMutableTransaction mtx;
    mtx.nVersion = 2;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    mtx.vin[0].prevout.hash = uint256::ZERO;
    mtx.vin[0].prevout.n = 0;
    mtx.vin[0].scriptSig = CScript();
    mtx.vin[0].nSequence = 0xFFFFFFFF;
    mtx.vout[0].nValue = 1000;
    mtx.vout[0].scriptPubKey = CScript() << OP_TRUE;
    mtx.nLockTime = 0;

    const CTransaction tx(mtx);
    const unsigned int nIn = 0;
    const CAmount amount = 0;

    // Create invalid signature (wrong size, but correct format)
    std::vector<unsigned char> invalidSig(DILITHIUM_SIGNATUREBYTES + 1, 0x42);
    invalidSig.back() = SIGHASH_ALL;

    // Build script: [num=3, invalid_sig, pubkey, OP_PQCHECKSIGADD] -> should result in 3
    CScript script;
    script << CScriptNum(3).getvch() << invalidSig << pk << OP_PQCHECKSIGADD;

    // Execute
    std::vector<std::vector<unsigned char>> stack;
    TransactionSignatureChecker checker(&tx, nIn, amount, MissingDataBehavior::ASSERT_FAIL);
    ScriptExecutionData execdata;
    ScriptError err = SCRIPT_ERR_OK;

    bool ok = EvalScript(stack, script, SCRIPT_VERIFY_NONE, checker, SigVersion::BASE, execdata, &err);

    BOOST_CHECK(ok);
    BOOST_CHECK(err == SCRIPT_ERR_OK);
    BOOST_CHECK(stack.size() == 1);

    // Verify counter was NOT incremented
    CScriptNum result(stack.back(), false);
    BOOST_CHECK(result == 3);
}

BOOST_AUTO_TEST_CASE(pqchecksigadd_empty_sig_leaves_counter_unchanged)
{
    // Generate PQ keys
    std::vector<unsigned char> pk(DILITHIUM_PUBLICKEYBYTES);
    PQ_GenerateKeypair(pk, std::vector<unsigned char>(DILITHIUM_SECRETKEYBYTES));

    // Build script: [num=7, empty_sig, pubkey, OP_PQCHECKSIGADD] -> should result in 7
    CScript script;
    std::vector<unsigned char> emptySig;
    script << CScriptNum(7).getvch() << emptySig << pk << OP_PQCHECKSIGADD;

    // Minimal transaction
    CMutableTransaction mtx;
    mtx.nVersion = 2;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    const CTransaction tx(mtx);
    const unsigned int nIn = 0;
    const CAmount amount = 0;

    // Execute
    std::vector<std::vector<unsigned char>> stack;
    TransactionSignatureChecker checker(&tx, nIn, amount, MissingDataBehavior::ASSERT_FAIL);
    ScriptExecutionData execdata;
    ScriptError err = SCRIPT_ERR_OK;

    bool ok = EvalScript(stack, script, SCRIPT_VERIFY_NONE, checker, SigVersion::BASE, execdata, &err);

    BOOST_CHECK(ok);
    BOOST_CHECK(err == SCRIPT_ERR_OK);
    BOOST_CHECK(stack.size() == 1);

    // Verify counter was NOT incremented (empty sig = invalid)
    CScriptNum result(stack.back(), false);
    BOOST_CHECK(result == 7);
}

BOOST_AUTO_TEST_SUITE_END()

