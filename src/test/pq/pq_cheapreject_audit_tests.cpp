#include <boost/test/unit_test.hpp>

#include "script/interpreter.h"
#include "script/script_error.h"
#include "crypto/dilithium.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "policy/policy.h"
#include <random>

BOOST_AUTO_TEST_SUITE(pq_cheapreject_audit_tests)

BOOST_AUTO_TEST_CASE(pq_audit_rejects_bad_hashtype_cheaply)
{
#ifdef ENABLE_PQ_TEST_HOOKS
    PQAuditReset();
    PQ_ResetVerifyCounter();

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    CTransaction tx(mtx);

    // Correct-sized, non-zero pubkey
    std::vector<unsigned char> pk(DILITHIUM_PUBLICKEYBYTES, 1);

    // Correct-sized signature with non-zero body and invalid hashtype
    std::vector<unsigned char> sig(DILITHIUM_SIGNATUREBYTES + 1, 0x00);
    sig[0] = 0x01; // Ensure body is NOT all-zero (set first byte to non-zero)
    sig.back() = 0x00; // invalid hashtype (MVP allows only 0x01 = SIGHASH_ALL)

    CScript scriptPubKey = CScript() << pk << OP_PQCHECKSIG;
    CScript scriptSig = CScript() << sig;

    PrecomputedTransactionData txdata;
    GenericTransactionSignatureChecker<CTransaction> checker(
        &tx, 0, 0, txdata, MissingDataBehavior::FAIL);

    ScriptError err = SCRIPT_ERR_OK;
    bool ok = VerifyScript(scriptSig, scriptPubKey, nullptr, STANDARD_SCRIPT_VERIFY_FLAGS, checker, &err);
    BOOST_CHECK(!ok);
    BOOST_TEST_MESSAGE(std::string("pq_audit_rejects_bad_hashtype_cheaply: ScriptError=") + ScriptErrorString(err) + " (int=" + std::to_string((int)err) + ")");
    BOOST_CHECK(err == SCRIPT_ERR_SIG_HASHTYPE);

    // Should reject before expensive operations
    BOOST_CHECK_EQUAL(PQAuditSigHashCalls(), 0U);
    BOOST_CHECK_EQUAL(PQAuditPQVerifyCalls(), 0U);
#endif
}

BOOST_AUTO_TEST_CASE(pq_audit_rejects_wrong_sig_size_cheaply)
{
#ifdef ENABLE_PQ_TEST_HOOKS
    PQAuditReset();
    PQ_ResetVerifyCounter();

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    CTransaction tx(mtx);

    std::vector<unsigned char> pk(DILITHIUM_PUBLICKEYBYTES, 1);
    // Wrong signature size (too small - missing hashtype byte)
    std::vector<unsigned char> sig(DILITHIUM_SIGNATUREBYTES, 2); // Should be DILITHIUM_SIGNATUREBYTES + 1

    CScript scriptPubKey = CScript() << pk << OP_PQCHECKSIG;
    CScript scriptSig = CScript() << sig;

    PrecomputedTransactionData txdata;
    GenericTransactionSignatureChecker<CTransaction> checker(
        &tx, 0, 0, txdata, MissingDataBehavior::FAIL);

    ScriptError err = SCRIPT_ERR_OK;
    bool ok = VerifyScript(scriptSig, scriptPubKey, nullptr, STANDARD_SCRIPT_VERIFY_FLAGS, checker, &err);
    BOOST_CHECK(!ok);
    BOOST_TEST_MESSAGE(std::string("pq_audit_rejects_wrong_sig_size_cheaply: ScriptError=") + ScriptErrorString(err) + " (int=" + std::to_string((int)err) + ")");
    BOOST_CHECK(err == SCRIPT_ERR_SIG_DER);

    // Should reject before expensive operations
    BOOST_CHECK_EQUAL(PQAuditSigHashCalls(), 0U);
    BOOST_CHECK_EQUAL(PQAuditPQVerifyCalls(), 0U);
    BOOST_CHECK_EQUAL(PQ_GetVerifyCounter(), 0U);
#endif
}

BOOST_AUTO_TEST_CASE(pq_audit_rejects_wrong_pubkey_size_cheaply)
{
#ifdef ENABLE_PQ_TEST_HOOKS
    PQAuditReset();
    PQ_ResetVerifyCounter();

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    CTransaction tx(mtx);

    // Wrong pubkey size (too small)
    std::vector<unsigned char> pk(DILITHIUM_PUBLICKEYBYTES - 1, 1);
    std::vector<unsigned char> sig(DILITHIUM_SIGNATUREBYTES + 1, 2);
    sig.back() = (unsigned char)SIGHASH_ALL;

    CScript scriptPubKey = CScript() << pk << OP_PQCHECKSIG;
    CScript scriptSig = CScript() << sig;

    PrecomputedTransactionData txdata;
    GenericTransactionSignatureChecker<CTransaction> checker(
        &tx, 0, 0, txdata, MissingDataBehavior::FAIL);

    ScriptError err = SCRIPT_ERR_OK;
    bool ok = VerifyScript(scriptSig, scriptPubKey, nullptr, STANDARD_SCRIPT_VERIFY_FLAGS, checker, &err);
    BOOST_CHECK(!ok);
    BOOST_TEST_MESSAGE(std::string("pq_audit_rejects_wrong_pubkey_size_cheaply: ScriptError=") + ScriptErrorString(err) + " (int=" + std::to_string((int)err) + ")");
    BOOST_CHECK(err == SCRIPT_ERR_PUBKEYTYPE);

    // Should reject before expensive operations
    BOOST_CHECK_EQUAL(PQAuditSigHashCalls(), 0U);
    BOOST_CHECK_EQUAL(PQAuditPQVerifyCalls(), 0U);
#endif
}

BOOST_AUTO_TEST_CASE(pq_audit_rejects_all_zero_pubkey_cheaply)
{
#ifdef ENABLE_PQ_TEST_HOOKS
    PQAuditReset();
    PQ_ResetVerifyCounter();

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    CTransaction tx(mtx);

    // All-zero pubkey
    std::vector<unsigned char> pk(DILITHIUM_PUBLICKEYBYTES, 0x00);
    std::vector<unsigned char> sig(DILITHIUM_SIGNATUREBYTES + 1, 0x00);
    sig.back() = (unsigned char)SIGHASH_ALL; // 0x01

    CScript scriptPubKey = CScript() << pk << OP_PQCHECKSIG;
    CScript scriptSig = CScript() << sig;

    PrecomputedTransactionData txdata;
    GenericTransactionSignatureChecker<CTransaction> checker(
        &tx, 0, 0, txdata, MissingDataBehavior::FAIL);

    ScriptError err = SCRIPT_ERR_OK;
    bool ok = VerifyScript(scriptSig, scriptPubKey, nullptr, STANDARD_SCRIPT_VERIFY_FLAGS, checker, &err);
    BOOST_CHECK(!ok);
    BOOST_TEST_MESSAGE(std::string("pq_audit_rejects_all_zero_pubkey_cheaply: ScriptError=") + ScriptErrorString(err) + " (int=" + std::to_string((int)err) + ")");
    BOOST_CHECK(err == SCRIPT_ERR_PUBKEYTYPE);

    // Should reject before expensive operations
    BOOST_CHECK_EQUAL(PQAuditSigHashCalls(), 0U);
    BOOST_CHECK_EQUAL(PQAuditPQVerifyCalls(), 0U);
    BOOST_CHECK_EQUAL(PQ_GetVerifyCounter(), 0U);
#endif
}

BOOST_AUTO_TEST_CASE(pq_audit_rejects_all_zero_sig_cheaply)
{
#ifdef ENABLE_PQ_TEST_HOOKS
    PQAuditReset();
    PQ_ResetVerifyCounter();

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    CTransaction tx(mtx);

    // Correct-sized, non-zero pubkey
    std::vector<unsigned char> pk(DILITHIUM_PUBLICKEYBYTES, 1);
    
    // All-zero signature body with valid hashtype
    std::vector<unsigned char> sig(DILITHIUM_SIGNATUREBYTES + 1, 0x00);
    // First DILITHIUM_SIGNATUREBYTES bytes are 0x00 (zero body)
    // Last byte (hashtype) is set to valid SIGHASH_ALL (0x01)
    sig.back() = (unsigned char)SIGHASH_ALL; // 0x01

    CScript scriptPubKey = CScript() << pk << OP_PQCHECKSIG;
    CScript scriptSig = CScript() << sig;

    PrecomputedTransactionData txdata;
    GenericTransactionSignatureChecker<CTransaction> checker(
        &tx, 0, 0, txdata, MissingDataBehavior::FAIL);

    ScriptError err = SCRIPT_ERR_OK;
    bool ok = VerifyScript(scriptSig, scriptPubKey, nullptr, STANDARD_SCRIPT_VERIFY_FLAGS, checker, &err);
    BOOST_CHECK(!ok);
    BOOST_TEST_MESSAGE(std::string("pq_audit_rejects_all_zero_sig_cheaply: ScriptError=") + ScriptErrorString(err) + " (int=" + std::to_string((int)err) + ")");
    BOOST_CHECK(err == SCRIPT_ERR_SIG_DER);

    // Should reject before expensive operations
    BOOST_CHECK_EQUAL(PQAuditSigHashCalls(), 0U);
    BOOST_CHECK_EQUAL(PQAuditPQVerifyCalls(), 0U);
    BOOST_CHECK_EQUAL(PQ_GetVerifyCounter(), 0U);
#endif
}

BOOST_AUTO_TEST_CASE(pq_audit_fuzz_invalid_inputs_cheaply)
{
#ifdef ENABLE_PQ_TEST_HOOKS
    std::mt19937 gen(42); // Fixed seed for determinism
    std::uniform_int_distribution<> size_dist(0, DILITHIUM_PUBLICKEYBYTES * 2);
    std::uniform_int_distribution<> byte_dist(0, 255);
    std::uniform_int_distribution<> hashtype_dist(0, 255);

    for (int i = 0; i < 1000; ++i) {
        PQAuditReset();
        PQ_ResetVerifyCounter();

        CMutableTransaction mtx;
        mtx.vin.resize(1);
        mtx.vout.resize(1);
        CTransaction tx(mtx);

        // Random wrong sizes around boundaries
        int pk_size = size_dist(gen);
        int sig_size = size_dist(gen);
        
        // Ensure at least one is wrong
        if (pk_size == DILITHIUM_PUBLICKEYBYTES && sig_size == DILITHIUM_SIGNATUREBYTES + 1) {
            // Force wrong size
            if (i % 2 == 0) {
                pk_size = DILITHIUM_PUBLICKEYBYTES - 1;
            } else {
                sig_size = DILITHIUM_SIGNATUREBYTES;
            }
        }

        std::vector<unsigned char> pk(pk_size);
        std::vector<unsigned char> sig(sig_size);

        // Random invalid hashtype values (not 0x01)
        unsigned char hashtype = hashtype_dist(gen);
        if (hashtype == SIGHASH_ALL) {
            hashtype = (hashtype + 1) % 256;
        }

        // Random zero-blob bodies
        bool zero_pubkey = (i % 3 == 0);
        bool zero_sig = (i % 5 == 0);

        if (zero_pubkey) {
            std::fill(pk.begin(), pk.end(), 0x00);
        } else {
            for (size_t j = 0; j < pk.size(); ++j) {
                pk[j] = byte_dist(gen);
            }
        }

        if (zero_sig) {
            std::fill(sig.begin(), sig.end(), 0x00);
        } else {
            for (size_t j = 0; j < sig.size(); ++j) {
                sig[j] = byte_dist(gen);
            }
        }

        // Add hashtype byte if sig is large enough
        if (sig.size() > 0) {
            sig.back() = hashtype;
        }

        CScript scriptPubKey = CScript() << pk << OP_PQCHECKSIG;
        CScript scriptSig = CScript() << sig;

        PrecomputedTransactionData txdata;
        GenericTransactionSignatureChecker<CTransaction> checker(
            &tx, 0, 0, txdata, MissingDataBehavior::FAIL);

        ScriptError err = SCRIPT_ERR_OK;
        bool ok = VerifyScript(scriptSig, scriptPubKey, nullptr, STANDARD_SCRIPT_VERIFY_FLAGS, checker, &err);
        
        // All invalid inputs should be rejected
        BOOST_CHECK(!ok);
        
        // Should reject before expensive operations
        BOOST_CHECK_EQUAL(PQAuditSigHashCalls(), 0U);
        BOOST_CHECK_EQUAL(PQAuditPQVerifyCalls(), 0U);
        BOOST_CHECK_EQUAL(PQ_GetVerifyCounter(), 0U);
    }
#endif
}

BOOST_AUTO_TEST_SUITE_END()
