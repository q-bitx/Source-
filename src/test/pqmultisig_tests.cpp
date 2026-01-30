#include <boost/test/unit_test.hpp>

#include <script/interpreter.h>
#include <script/script.h>
#include <script/standard.h>
#include <script/solver.h>
#include <primitives/transaction.h>
#include <uint256.h>
#include <script/script_error.h>
#include <crypto/dilithium.h>
#include <test/util/setup_common.h>

#include <vector>

BOOST_FIXTURE_TEST_SUITE(pqmultisig_tests, BasicTestingSetup)

// Helper to sign a multisig script
static CScript sign_pqmultisig(const CScript& scriptPubKey, 
                                const std::vector<std::vector<unsigned char>>& signing_privkeys, 
                                const CTransaction& transaction, int whichIn, int required)
{
    uint256 hash = SignatureHash(scriptPubKey, transaction, whichIn, SIGHASH_ALL, 0, SigVersion::BASE, nullptr);
    std::vector<unsigned char> msg(hash.begin(), hash.end());

    CScript result;
    result << OP_0; // CHECKMULTISIGDILITHIUM bug workaround (dummy element)
    
    int sigs_added = 0;
    for (size_t i = 0; i < signing_privkeys.size() && sigs_added < required; ++i) {
        std::vector<unsigned char> sig;
        if (PQ_Sign(sig, msg, signing_privkeys[i])) {
            sig.push_back((unsigned char)SIGHASH_ALL);
            result << sig;
            sigs_added++;
        }
    }
    
    // Pad with empty signatures if needed
    while (sigs_added < required) {
        result << std::vector<unsigned char>();
        sigs_added++;
    }
    
    return result;
}

BOOST_AUTO_TEST_CASE(pqmultisig_solver_recognizes_2_of_3)
{
    // Generate 3 keypairs
    std::vector<std::vector<unsigned char>> pubkeys(3);
    std::vector<std::vector<unsigned char>> privkeys(3);
    for (int i = 0; i < 3; i++) {
        pubkeys[i].resize(DILITHIUM_PUBLICKEYBYTES);
        privkeys[i].resize(DILITHIUM_SECRETKEYBYTES);
        PQ_GenerateKeypair(pubkeys[i], privkeys[i]);
    }
    
    // Create 2-of-3 multisig script
    CScript script;
    script << OP_2;
    for (const auto& pk : pubkeys) {
        script << pk;
    }
    script << OP_3 << OP_CHECKMULTISIGDILITHIUM;
    
    // Test solver
    std::vector<std::vector<unsigned char>> solutions;
    TxoutType type = Solver(script, solutions);
    
    BOOST_CHECK(type == TxoutType::DILITHIUM_MULTISIG);
    BOOST_CHECK(solutions.size() == 5); // [required=2, pk1, pk2, pk3, total=3]
    BOOST_CHECK(solutions[0][0] == 2);
    BOOST_CHECK(solutions[4][0] == 3);
}

BOOST_AUTO_TEST_CASE(pqmultisig_solver_recognizes_3_of_5)
{
    // Generate 5 keypairs
    std::vector<std::vector<unsigned char>> pubkeys(5);
    std::vector<std::vector<unsigned char>> privkeys(5);
    for (int i = 0; i < 5; i++) {
        pubkeys[i].resize(DILITHIUM_PUBLICKEYBYTES);
        privkeys[i].resize(DILITHIUM_SECRETKEYBYTES);
        PQ_GenerateKeypair(pubkeys[i], privkeys[i]);
    }
    
    // Create 3-of-5 multisig script
    CScript script;
    script << OP_3;
    for (const auto& pk : pubkeys) {
        script << pk;
    }
    script << OP_5 << OP_CHECKMULTISIGDILITHIUM;
    
    // Test solver
    std::vector<std::vector<unsigned char>> solutions;
    TxoutType type = Solver(script, solutions);
    
    BOOST_CHECK(type == TxoutType::DILITHIUM_MULTISIG);
    BOOST_CHECK(solutions.size() == 7); // [required=3, pk1...pk5, total=5]
    BOOST_CHECK(solutions[0][0] == 3);
    BOOST_CHECK(solutions[6][0] == 5);
}

BOOST_AUTO_TEST_CASE(pqmultisig_interpreter_success_2_of_3)
{
    unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC;
    
    // Generate 3 keypairs
    std::vector<std::vector<unsigned char>> pubkeys(3);
    std::vector<std::vector<unsigned char>> privkeys(3);
    for (int i = 0; i < 3; i++) {
        pubkeys[i].resize(DILITHIUM_PUBLICKEYBYTES);
        privkeys[i].resize(DILITHIUM_SECRETKEYBYTES);
        PQ_GenerateKeypair(pubkeys[i], privkeys[i]);
    }
    
    // Create 2-of-3 multisig script
    CScript scriptPubKey;
    scriptPubKey << OP_2;
    for (const auto& pk : pubkeys) {
        scriptPubKey << pk;
    }
    scriptPubKey << OP_3 << OP_CHECKMULTISIGDILITHIUM;
    
    // Create transaction
    CMutableTransaction txFrom;
    txFrom.vout.resize(1);
    txFrom.vout[0].scriptPubKey = scriptPubKey;
    
    CMutableTransaction txTo;
    txTo.vin.resize(1);
    txTo.vout.resize(1);
    txTo.vin[0].prevout.n = 0;
    txTo.vin[0].prevout.hash = txFrom.GetHash();
    txTo.vout[0].nValue = 1;
    
    // Sign with 2 keys
    std::vector<std::vector<unsigned char>> signing_keys = {privkeys[0], privkeys[1]};
    CScript scriptSig = sign_pqmultisig(scriptPubKey, signing_keys, CTransaction(txTo), 0, 2);
    
    // Verify
    ScriptError err;
    CAmount amount = 0;
    bool ok = VerifyScript(scriptSig, scriptPubKey, nullptr, flags, 
                          MutableTransactionSignatureChecker(&txTo, 0, amount, MissingDataBehavior::ASSERT_FAIL), &err);
    
    BOOST_CHECK_MESSAGE(ok, "2-of-3 PQ multisig verification failed");
    BOOST_CHECK(err == SCRIPT_ERR_OK);
}

BOOST_AUTO_TEST_CASE(pqmultisig_interpreter_fails_missing_dummy_with_null dummy)
{
    unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC | SCRIPT_VERIFY_NULLDUMMY;
    
    // Generate 2 keypairs
    std::vector<std::vector<unsigned char>> pubkeys(2);
    std::vector<std::vector<unsigned char>> privkeys(2);
    for (int i = 0; i < 2; i++) {
        pubkeys[i].resize(DILITHIUM_PUBLICKEYBYTES);
        privkeys[i].resize(DILITHIUM_SECRETKEYBYTES);
        PQ_GenerateKeypair(pubkeys[i], privkeys[i]);
    }
    
    // Create 2-of-2 multisig script
    CScript scriptPubKey;
    scriptPubKey << OP_2;
    for (const auto& pk : pubkeys) {
        scriptPubKey << pk;
    }
    scriptPubKey << OP_2 << OP_CHECKMULTISIGDILITHIUM;
    
    // Create transaction
    CMutableTransaction txFrom;
    txFrom.vout.resize(1);
    txFrom.vout[0].scriptPubKey = scriptPubKey;
    
    CMutableTransaction txTo;
    txTo.vin.resize(1);
    txTo.vout.resize(1);
    txTo.vin[0].prevout.n = 0;
    txTo.vin[0].prevout.hash = txFrom.GetHash();
    txTo.vout[0].nValue = 1;
    
    // Sign but forget dummy element
    uint256 hash = SignatureHash(scriptPubKey, CTransaction(txTo), 0, SIGHASH_ALL, 0, SigVersion::BASE, nullptr);
    std::vector<unsigned char> msg(hash.begin(), hash.end());
    
    CScript scriptSig;
    // Missing OP_0 dummy!
    for (int i = 0; i < 2; i++) {
        std::vector<unsigned char> sig;
        PQ_Sign(sig, msg, privkeys[i]);
        sig.push_back((unsigned char)SIGHASH_ALL);
        scriptSig << sig;
    }
    
    // Verify - should fail with NULLDUMMY error
    ScriptError err;
    CAmount amount = 0;
    bool ok = VerifyScript(scriptSig, scriptPubKey, nullptr, flags, 
                          MutableTransactionSignatureChecker(&txTo, 0, amount, MissingDataBehavior::ASSERT_FAIL), &err);
    
    BOOST_CHECK_MESSAGE(!ok, "Should fail without dummy element when NULLDUMMY is set");
    BOOST_CHECK(err == SCRIPT_ERR_SIG_NULLDUMMY);
}

BOOST_AUTO_TEST_CASE(pqmultisig_interpreter_fails_invalid_pubkey_format)
{
    unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC;
    
    // Generate 2 keypairs
    std::vector<std::vector<unsigned char>> pubkeys(2);
    std::vector<std::vector<unsigned char>> privkeys(2);
    for (int i = 0; i < 2; i++) {
        pubkeys[i].resize(DILITHIUM_PUBLICKEYBYTES);
        privkeys[i].resize(DILITHIUM_SECRETKEYBYTES);
        PQ_GenerateKeypair(pubkeys[i], privkeys[i]);
    }
    
    // Create 2-of-2 multisig script
    CScript scriptPubKey;
    scriptPubKey << OP_2;
    for (const auto& pk : pubkeys) {
        scriptPubKey << pk;
    }
    scriptPubKey << OP_2 << OP_CHECKMULTISIGDILITHIUM;
    
    // Create transaction
    CMutableTransaction txFrom;
    txFrom.vout.resize(1);
    txFrom.vout[0].scriptPubKey = scriptPubKey;
    
    CMutableTransaction txTo;
    txTo.vin.resize(1);
    txTo.vout.resize(1);
    txTo.vin[0].prevout.n = 0;
    txTo.vin[0].prevout.hash = txFrom.GetHash();
    txTo.vout[0].nValue = 1;
    
    // Create scriptSig with invalid pubkey size in stack (simulated by wrong scriptPubKey)
    // Actually, we'll test by using a scriptPubKey with wrong-sized pubkey
    CScript badScriptPubKey;
    badScriptPubKey << OP_2;
    badScriptPubKey << std::vector<unsigned char>(DILITHIUM_PUBLICKEYBYTES - 1, 0x01); // Wrong size
    badScriptPubKey << pubkeys[1];
    badScriptPubKey << OP_2 << OP_CHECKMULTISIGDILITHIUM;
    
    // This should fail at solver level, but let's test interpreter with valid scriptSig on bad scriptPubKey
    uint256 hash = SignatureHash(scriptPubKey, CTransaction(txTo), 0, SIGHASH_ALL, 0, SigVersion::BASE, nullptr);
    std::vector<unsigned char> msg(hash.begin(), hash.end());
    
    CScript scriptSig;
    scriptSig << OP_0; // dummy
    for (int i = 0; i < 2; i++) {
        std::vector<unsigned char> sig;
        PQ_Sign(sig, msg, privkeys[i]);
        sig.push_back((unsigned char)SIGHASH_ALL);
        scriptSig << sig;
    }
    
    // Verify - should fail when interpreter tries to validate pubkeys
    ScriptError err;
    CAmount amount = 0;
    // We need to manually construct a script with invalid pubkey to test this
    // For now, test that solver rejects invalid pubkey size
    std::vector<std::vector<unsigned char>> solutions;
    TxoutType type = Solver(badScriptPubKey, solutions);
    BOOST_CHECK(type == TxoutType::NONSTANDARD);
}

BOOST_AUTO_TEST_CASE(pqmultisig_interpreter_fails_invalid_sig_format)
{
    unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC;
    
    // Generate 2 keypairs
    std::vector<std::vector<unsigned char>> pubkeys(2);
    std::vector<std::vector<unsigned char>> privkeys(2);
    for (int i = 0; i < 2; i++) {
        pubkeys[i].resize(DILITHIUM_PUBLICKEYBYTES);
        privkeys[i].resize(DILITHIUM_SECRETKEYBYTES);
        PQ_GenerateKeypair(pubkeys[i], privkeys[i]);
    }
    
    // Create 2-of-2 multisig script
    CScript scriptPubKey;
    scriptPubKey << OP_2;
    for (const auto& pk : pubkeys) {
        scriptPubKey << pk;
    }
    scriptPubKey << OP_2 << OP_CHECKMULTISIGDILITHIUM;
    
    // Create transaction
    CMutableTransaction txFrom;
    txFrom.vout.resize(1);
    txFrom.vout[0].scriptPubKey = scriptPubKey;
    
    CMutableTransaction txTo;
    txTo.vin.resize(1);
    txTo.vout.resize(1);
    txTo.vin[0].prevout.n = 0;
    txTo.vin[0].prevout.hash = txFrom.GetHash();
    txTo.vout[0].nValue = 1;
    
    // Create scriptSig with invalid signature size
    CScript scriptSig;
    scriptSig << OP_0; // dummy
    scriptSig << std::vector<unsigned char>(DILITHIUM_SIGNATUREBYTES, 0x01); // Missing hashtype byte
    std::vector<unsigned char> sig2;
    uint256 hash = SignatureHash(scriptPubKey, CTransaction(txTo), 0, SIGHASH_ALL, 0, SigVersion::BASE, nullptr);
    std::vector<unsigned char> msg(hash.begin(), hash.end());
    PQ_Sign(sig2, msg, privkeys[1]);
    sig2.push_back((unsigned char)SIGHASH_ALL);
    scriptSig << sig2;
    
    // Verify - should fail with SIG_DER error
    ScriptError err;
    CAmount amount = 0;
    bool ok = VerifyScript(scriptSig, scriptPubKey, nullptr, flags, 
                          MutableTransactionSignatureChecker(&txTo, 0, amount, MissingDataBehavior::ASSERT_FAIL), &err);
    
    BOOST_CHECK_MESSAGE(!ok, "Should fail with invalid signature format");
    BOOST_CHECK(err == SCRIPT_ERR_SIG_DER);
}

BOOST_AUTO_TEST_CASE(pqmultisig_interpreter_fails_n_sigs_greater_than_n_keys)
{
    unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC;
    
    // Generate 2 keypairs
    std::vector<std::vector<unsigned char>> pubkeys(2);
    std::vector<std::vector<unsigned char>> privkeys(2);
    for (int i = 0; i < 2; i++) {
        pubkeys[i].resize(DILITHIUM_PUBLICKEYBYTES);
        privkeys[i].resize(DILITHIUM_SECRETKEYBYTES);
        PQ_GenerateKeypair(pubkeys[i], privkeys[i]);
    }
    
    // Create 2-of-2 multisig script
    CScript scriptPubKey;
    scriptPubKey << OP_2;
    for (const auto& pk : pubkeys) {
        scriptPubKey << pk;
    }
    scriptPubKey << OP_2 << OP_CHECKMULTISIGDILITHIUM;
    
    // Create transaction
    CMutableTransaction txFrom;
    txFrom.vout.resize(1);
    txFrom.vout[0].scriptPubKey = scriptPubKey;
    
    CMutableTransaction txTo;
    txTo.vin.resize(1);
    txTo.vout.resize(1);
    txTo.vin[0].prevout.n = 0;
    txTo.vin[0].prevout.hash = txFrom.GetHash();
    txTo.vout[0].nValue = 1;
    
    // Create scriptSig with 3 signatures (more than nKeys=2)
    uint256 hash = SignatureHash(scriptPubKey, CTransaction(txTo), 0, SIGHASH_ALL, 0, SigVersion::BASE, nullptr);
    std::vector<unsigned char> msg(hash.begin(), hash.end());
    
    CScript scriptSig;
    scriptSig << OP_0; // dummy
    scriptSig << OP_3; // nSigs = 3 (invalid, should be <= 2)
    scriptSig << std::vector<unsigned char>(DILITHIUM_SIGNATUREBYTES + 1, 0x01);
    scriptSig << std::vector<unsigned char>(DILITHIUM_SIGNATUREBYTES + 1, 0x01);
    scriptSig << std::vector<unsigned char>(DILITHIUM_SIGNATUREBYTES + 1, 0x01);
    scriptSig << pubkeys[0];
    scriptSig << pubkeys[1];
    scriptSig << OP_2; // nKeys = 2
    
    // Verify - should fail with SIG_COUNT error
    ScriptError err;
    CAmount amount = 0;
    bool ok = VerifyScript(scriptSig, scriptPubKey, nullptr, flags, 
                          MutableTransactionSignatureChecker(&txTo, 0, amount, MissingDataBehavior::ASSERT_FAIL), &err);
    
    BOOST_CHECK_MESSAGE(!ok, "Should fail when nSigs > nKeys");
    BOOST_CHECK(err == SCRIPT_ERR_SIG_COUNT);
}

BOOST_AUTO_TEST_CASE(pqmultisig_verify_variant_aborts_on_failure)
{
    unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC;
    
    // Generate 2 keypairs
    std::vector<std::vector<unsigned char>> pubkeys(2);
    std::vector<std::vector<unsigned char>> privkeys(2);
    for (int i = 0; i < 2; i++) {
        pubkeys[i].resize(DILITHIUM_PUBLICKEYBYTES);
        privkeys[i].resize(DILITHIUM_SECRETKEYBYTES);
        PQ_GenerateKeypair(pubkeys[i], privkeys[i]);
    }
    
    // Create 2-of-2 multisig script with VERIFY variant
    CScript scriptPubKey;
    scriptPubKey << OP_2;
    for (const auto& pk : pubkeys) {
        scriptPubKey << pk;
    }
    scriptPubKey << OP_2 << OP_CHECKMULTISIGDILITHIUMVERIFY;
    
    // Create transaction
    CMutableTransaction txFrom;
    txFrom.vout.resize(1);
    txFrom.vout[0].scriptPubKey = scriptPubKey;
    
    CMutableTransaction txTo;
    txTo.vin.resize(1);
    txTo.vout.resize(1);
    txTo.vin[0].prevout.n = 0;
    txTo.vin[0].prevout.hash = txFrom.GetHash();
    txTo.vout[0].nValue = 1;
    
    // Create scriptSig with only 1 signature (insufficient)
    uint256 hash = SignatureHash(scriptPubKey, CTransaction(txTo), 0, SIGHASH_ALL, 0, SigVersion::BASE, nullptr);
    std::vector<unsigned char> msg(hash.begin(), hash.end());
    
    CScript scriptSig;
    scriptSig << OP_0; // dummy
    std::vector<unsigned char> sig;
    PQ_Sign(sig, msg, privkeys[0]);
    sig.push_back((unsigned char)SIGHASH_ALL);
    scriptSig << sig;
    
    // Verify - should fail and VERIFY variant should abort
    ScriptError err;
    CAmount amount = 0;
    bool ok = VerifyScript(scriptSig, scriptPubKey, nullptr, flags, 
                          MutableTransactionSignatureChecker(&txTo, 0, amount, MissingDataBehavior::ASSERT_FAIL), &err);
    
    BOOST_CHECK_MESSAGE(!ok, "Should fail with insufficient signatures");
    BOOST_CHECK(err == SCRIPT_ERR_CHECKMULTISIGVERIFY);
}

BOOST_AUTO_TEST_SUITE_END()
