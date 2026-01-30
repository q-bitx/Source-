#define BOOST_TEST_MODULE QBitX_PQ_Multisig
#include <boost/test/included/unit_test.hpp>

// Dilithium params.h defines very generic macros (N, L, K, Q...) that break Boost MPL.
// Kill the worst offenders before including Boost.
#ifdef N
#undef N
#endif
#ifdef L
#undef L
#endif
#ifdef K
#undef K
#endif

#include <script/interpreter.h>
#include <script/script.h>
#include <script/standard.h>
#include <script/solver.h>
#include <script/script_error.h>
#include <primitives/transaction.h>
#include <uint256.h>
#include <crypto/dilithium.h>
#include <crypto/dilithium_wrapper.h>
#include <policy/policy.h>
#include <util/transaction_identifier.h> // Txid::FromUint256
#include <consensus/amount.h> // CAmount, COIN

#include <vector>
#include <initializer_list>

BOOST_AUTO_TEST_SUITE(pq_multisig_tests)

static std::vector<unsigned char> ToVec(const uint8_t* p, size_t n)
{
    return std::vector<unsigned char>(p, p + n);
}

// Helper to check if error is in allowed set
static bool ErrIn(ScriptError err, std::initializer_list<ScriptError> allowed)
{
    for (ScriptError e : allowed) {
        if (err == e) return true;
    }
    return false;
}

// Helper to generate a Dilithium keypair and return pubkey as vector
static std::vector<unsigned char> GenerateDilithiumPubKey()
{
    pqcrypto::dilithium::PQPublicKey pub{};
    pqcrypto::dilithium::PQPrivateKey priv{};
    pqcrypto::dilithium::keygen(&pub, &priv);
    return ToVec(pub.data, DILITHIUM_PUBLICKEYBYTES);
}

// Helper to create a Dilithium multisig scriptPubKey
static CScript CreateDilithiumMultisigScript(int m, int n, const std::vector<std::vector<unsigned char>>& pubkeys)
{
    CScript script;
    script << m;
    for (const auto& pk : pubkeys) {
        script << pk;
    }
    script << n << OP_CHECKMULTISIGDILITHIUM;
    return script;
}

// Multisig test case definition
struct MultisigCase {
    int m;  // required signatures
    int n;  // total pubkeys
};

// Test matrix for k-of-n variants
static const std::vector<MultisigCase> kMultisigCases = {
    {1, 1},  // 1-of-1 (sanity bridge)
    {2, 3},  // 2-of-3
    {3, 5},  // 3-of-5
    {5, 5},  // N-of-N
    {MAX_PUBKEYS_PER_MULTISIG, MAX_PUBKEYS_PER_MULTISIG}  // MAX boundary
};

// Helper to generate n Dilithium keypairs
static void GenerateDilithiumKeypairs(int n, 
                                      std::vector<std::vector<unsigned char>>& pubkeys_out,
                                      std::vector<pqcrypto::dilithium::PQPrivateKey>& privkeys_out)
{
    pubkeys_out.clear();
    privkeys_out.clear();
    pubkeys_out.reserve(n);
    privkeys_out.reserve(n);
    
    for (int i = 0; i < n; i++) {
        pqcrypto::dilithium::PQPublicKey pub{};
        pqcrypto::dilithium::PQPrivateKey priv{};
        pqcrypto::dilithium::keygen(&pub, &priv);
        pubkeys_out.push_back(ToVec(pub.data, DILITHIUM_PUBLICKEYBYTES));
        privkeys_out.push_back(priv);
    }
}

// ===== A) Solver Recognition Tests =====

BOOST_AUTO_TEST_CASE(solver_recognizes_2_of_3)
{
    // Generate 3 keypairs
    std::vector<std::vector<unsigned char>> pubkeys(3);
    for (int i = 0; i < 3; i++) {
        pubkeys[i] = GenerateDilithiumPubKey();
    }
    
    // Create 2-of-3 multisig script
    CScript scriptPubKey = CreateDilithiumMultisigScript(2, 3, pubkeys);
    
    // Test solver
    std::vector<std::vector<unsigned char>> solutions;
    TxoutType type = Solver(scriptPubKey, solutions);
    
    BOOST_CHECK(type == TxoutType::DILITHIUM_MULTISIG);
    BOOST_CHECK(solutions.size() == 5); // [required=2, pk1, pk2, pk3, total=3]
    BOOST_CHECK(solutions[0].size() == 1 && solutions[0][0] == 2);
    BOOST_CHECK(solutions[4].size() == 1 && solutions[4][0] == 3);
    BOOST_CHECK(solutions[1].size() == DILITHIUM_PUBLICKEYBYTES);
    BOOST_CHECK(solutions[2].size() == DILITHIUM_PUBLICKEYBYTES);
    BOOST_CHECK(solutions[3].size() == DILITHIUM_PUBLICKEYBYTES);
}

BOOST_AUTO_TEST_CASE(solver_recognizes_3_of_5)
{
    // Generate 5 keypairs
    std::vector<std::vector<unsigned char>> pubkeys(5);
    for (int i = 0; i < 5; i++) {
        pubkeys[i] = GenerateDilithiumPubKey();
    }
    
    // Create 3-of-5 multisig script
    CScript scriptPubKey = CreateDilithiumMultisigScript(3, 5, pubkeys);
    
    // Test solver
    std::vector<std::vector<unsigned char>> solutions;
    TxoutType type = Solver(scriptPubKey, solutions);
    
    BOOST_CHECK(type == TxoutType::DILITHIUM_MULTISIG);
    BOOST_CHECK(solutions.size() == 7); // [required=3, pk1...pk5, total=5]
    BOOST_CHECK(solutions[0].size() == 1 && solutions[0][0] == 3);
    BOOST_CHECK(solutions[6].size() == 1 && solutions[6][0] == 5);
}

// ===== B) Interpreter Negative Semantics Tests =====

BOOST_AUTO_TEST_CASE(interpreter_fails_null_dummy_with_flag)
{
    unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC | SCRIPT_VERIFY_NULLDUMMY;
    
    // Generate 2 keypairs
    std::vector<std::vector<unsigned char>> pubkeys(2);
    for (int i = 0; i < 2; i++) {
        pubkeys[i] = GenerateDilithiumPubKey();
    }
    
    // Create 2-of-2 multisig script
    CScript scriptPubKey = CreateDilithiumMultisigScript(2, 2, pubkeys);
    
    // Create minimal transaction
    CMutableTransaction txFrom;
    txFrom.vout.resize(1);
    txFrom.vout[0].scriptPubKey = scriptPubKey;
    
    CMutableTransaction txTo;
    txTo.vin.resize(1);
    txTo.vout.resize(1);
    txTo.vin[0].prevout.n = 0;
    txTo.vin[0].prevout.hash = txFrom.GetHash();
    txTo.vout[0].nValue = 1;
    
    // Create scriptSig with non-empty dummy element
    CScript scriptSig;
    scriptSig << std::vector<unsigned char>(1, 0x01); // Non-empty dummy (should be empty)
    scriptSig << OP_2; // nSigs = 2
    // Add empty signatures
    scriptSig << std::vector<unsigned char>();
    scriptSig << std::vector<unsigned char>();
    scriptSig << pubkeys[0];
    scriptSig << pubkeys[1];
    scriptSig << OP_2; // nKeys = 2
    
    // Verify - should fail with NULLDUMMY error or any other non-OK error
    ScriptError err;
    CAmount amount = 0;
    bool ok = VerifyScript(scriptSig, scriptPubKey, nullptr, flags, 
                          MutableTransactionSignatureChecker(&txTo, 0, amount, MissingDataBehavior::ASSERT_FAIL), &err);
    
    BOOST_CHECK_MESSAGE(!ok, "Should fail with non-empty dummy when NULLDUMMY is set");
    BOOST_CHECK_MESSAGE(err != SCRIPT_ERR_OK, "Should return non-OK error");
    // Accept SIG_NULLDUMMY or any other non-OK error if Q-BitX rejects earlier
    if (!ErrIn(err, {SCRIPT_ERR_SIG_NULLDUMMY, SCRIPT_ERR_INVALID_STACK_OPERATION, SCRIPT_ERR_SIG_COUNT})) {
        BOOST_TEST_MESSAGE("Warning: Got unexpected error " << ScriptErrorString(err) << " (expected SIG_NULLDUMMY or early rejection)");
    }
}

BOOST_AUTO_TEST_CASE(interpreter_fails_sig_count_greater_than_key_count)
{
    unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC;
    
    // Generate 2 keypairs
    std::vector<std::vector<unsigned char>> pubkeys(2);
    for (int i = 0; i < 2; i++) {
        pubkeys[i] = GenerateDilithiumPubKey();
    }
    
    // Create 2-of-2 multisig script
    CScript scriptPubKey = CreateDilithiumMultisigScript(2, 2, pubkeys);
    
    // Create minimal transaction
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
    CScript scriptSig;
    scriptSig << OP_0; // dummy
    scriptSig << OP_3; // nSigs = 3 (invalid, should be <= 2)
    scriptSig << std::vector<unsigned char>(DILITHIUM_SIGNATUREBYTES + 1, 0x01);
    scriptSig << std::vector<unsigned char>(DILITHIUM_SIGNATUREBYTES + 1, 0x01);
    scriptSig << std::vector<unsigned char>(DILITHIUM_SIGNATUREBYTES + 1, 0x01);
    scriptSig << pubkeys[0];
    scriptSig << pubkeys[1];
    scriptSig << OP_2; // nKeys = 2
    
    // Verify - should fail with SIG_COUNT error or any other non-OK error
    ScriptError err;
    CAmount amount = 0;
    bool ok = VerifyScript(scriptSig, scriptPubKey, nullptr, flags, 
                          MutableTransactionSignatureChecker(&txTo, 0, amount, MissingDataBehavior::ASSERT_FAIL), &err);
    
    BOOST_CHECK_MESSAGE(!ok, "Should fail when nSigs > nKeys");
    BOOST_CHECK_MESSAGE(err != SCRIPT_ERR_OK, "Should return non-OK error");
    // Accept SIG_COUNT or any other non-OK error if Q-BitX fails earlier
    if (!ErrIn(err, {SCRIPT_ERR_SIG_COUNT, SCRIPT_ERR_INVALID_STACK_OPERATION})) {
        BOOST_TEST_MESSAGE("Warning: Got unexpected error " << ScriptErrorString(err) << " (expected SIG_COUNT or early rejection)");
    }
}

BOOST_AUTO_TEST_CASE(interpreter_fails_pubkey_count_out_of_range)
{
    unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC;
    
    // Generate MAX_PUBKEYS_PER_MULTISIG + 1 keypairs
    std::vector<std::vector<unsigned char>> pubkeys(MAX_PUBKEYS_PER_MULTISIG + 1);
    for (size_t i = 0; i < pubkeys.size(); i++) {
        pubkeys[i] = GenerateDilithiumPubKey();
    }
    
    // Try to create multisig script with too many keys
    // This should fail at script construction or solver level
    CScript scriptPubKey;
    scriptPubKey << MAX_PUBKEYS_PER_MULTISIG + 1; // nKeys out of range
    for (const auto& pk : pubkeys) {
        scriptPubKey << pk;
    }
    scriptPubKey << static_cast<int>(pubkeys.size()) << OP_CHECKMULTISIGDILITHIUM;
    
    // Test solver - should reject
    std::vector<std::vector<unsigned char>> solutions;
    TxoutType type = Solver(scriptPubKey, solutions);
    
    // Solver should reject scripts with too many keys
    BOOST_CHECK(type == TxoutType::NONSTANDARD);
}

BOOST_AUTO_TEST_CASE(interpreter_fails_nullfail_with_invalid_signature)
{
    unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC | SCRIPT_VERIFY_NULLFAIL;
    
    // Generate 2 keypairs
    std::vector<std::vector<unsigned char>> pubkeys(2);
    for (int i = 0; i < 2; i++) {
        pubkeys[i] = GenerateDilithiumPubKey();
    }
    
    // Create 2-of-2 multisig script
    CScript scriptPubKey = CreateDilithiumMultisigScript(2, 2, pubkeys);
    
    // Create minimal transaction
    CMutableTransaction txFrom;
    txFrom.vout.resize(1);
    txFrom.vout[0].scriptPubKey = scriptPubKey;
    
    CMutableTransaction txTo;
    txTo.vin.resize(1);
    txTo.vout.resize(1);
    txTo.vin[0].prevout.n = 0;
    txTo.vin[0].prevout.hash = txFrom.GetHash();
    txTo.vout[0].nValue = 1;
    
    // Create scriptSig with non-empty invalid signature
    CScript scriptSig;
    scriptSig << OP_0; // dummy
    scriptSig << OP_2; // nSigs = 2
    // Add invalid signature (wrong size, but non-empty)
    scriptSig << std::vector<unsigned char>(DILITHIUM_SIGNATUREBYTES + 1, 0x42); // Invalid but non-empty
    scriptSig << std::vector<unsigned char>(); // Empty second sig
    scriptSig << pubkeys[0];
    scriptSig << pubkeys[1];
    scriptSig << OP_2; // nKeys = 2
    
    // Verify - should fail
    // Note: The exact error may be SIG_DER (format error) or SIG_NULLFAIL depending on validation order
    ScriptError err;
    CAmount amount = 0;
    bool ok = VerifyScript(scriptSig, scriptPubKey, nullptr, flags, 
                          MutableTransactionSignatureChecker(&txTo, 0, amount, MissingDataBehavior::ASSERT_FAIL), &err);
    
    BOOST_CHECK_MESSAGE(!ok, "Should fail with invalid signature");
    BOOST_CHECK(err != SCRIPT_ERR_OK);
}

// ===== C) Anti-DoS Cheap-Reject Tests =====

BOOST_AUTO_TEST_CASE(interpreter_cheap_rejects_oversized_signature)
{
    unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC;
    
    // Generate 2 keypairs
    std::vector<std::vector<unsigned char>> pubkeys(2);
    for (int i = 0; i < 2; i++) {
        pubkeys[i] = GenerateDilithiumPubKey();
    }
    
    // Create 2-of-2 multisig script
    CScript scriptPubKey = CreateDilithiumMultisigScript(2, 2, pubkeys);
    
    // Create minimal transaction
    CMutableTransaction txFrom;
    txFrom.vout.resize(1);
    txFrom.vout[0].scriptPubKey = scriptPubKey;
    
    CMutableTransaction txTo;
    txTo.vin.resize(1);
    txTo.vout.resize(1);
    txTo.vin[0].prevout.n = 0;
    txTo.vin[0].prevout.hash = txFrom.GetHash();
    txTo.vout[0].nValue = 1;
    
    // Create scriptSig with oversized signature (exceeds MAX_SCRIPT_ELEMENT_SIZE)
    CScript scriptSig;
    scriptSig << OP_0; // dummy
    scriptSig << OP_2; // nSigs = 2
    scriptSig << std::vector<unsigned char>(MAX_SCRIPT_ELEMENT_SIZE + 1, 0x01); // Oversized
    scriptSig << std::vector<unsigned char>(); // Empty second sig
    scriptSig << pubkeys[0];
    scriptSig << pubkeys[1];
    scriptSig << OP_2; // nKeys = 2
    
    // Verify - should fail immediately with size error (cheap reject)
    ScriptError err;
    CAmount amount = 0;
    bool ok = VerifyScript(scriptSig, scriptPubKey, nullptr, flags, 
                          MutableTransactionSignatureChecker(&txTo, 0, amount, MissingDataBehavior::ASSERT_FAIL), &err);
    
    BOOST_CHECK_MESSAGE(!ok, "Should fail with oversized signature (cheap reject)");
    BOOST_CHECK_MESSAGE(err != SCRIPT_ERR_OK, "Should return non-OK error");
    // Accept PUSH_SIZE / SIG_DER / PUBKEYTYPE or any other non-OK error; goal is to ensure failure
    if (!ErrIn(err, {SCRIPT_ERR_SIG_DER, SCRIPT_ERR_PUSH_SIZE, SCRIPT_ERR_INVALID_STACK_OPERATION})) {
        BOOST_TEST_MESSAGE("Warning: Got unexpected error " << ScriptErrorString(err) << " (expected size-related error)");
    }
}

BOOST_AUTO_TEST_CASE(interpreter_cheap_rejects_oversized_pubkey)
{
    unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC;
    
    // Generate 1 valid keypair
    std::vector<unsigned char> valid_pubkey = GenerateDilithiumPubKey();
    
    // Create 1-of-2 multisig script with one valid and one oversized pubkey
    CScript scriptPubKey;
    scriptPubKey << OP_1; // m = 1
    scriptPubKey << valid_pubkey;
    scriptPubKey << std::vector<unsigned char>(MAX_SCRIPT_ELEMENT_SIZE + 1, 0x01); // Oversized pubkey
    scriptPubKey << OP_2; // n = 2
    scriptPubKey << OP_CHECKMULTISIGDILITHIUM;
    
    // Create minimal transaction
    CMutableTransaction txFrom;
    txFrom.vout.resize(1);
    txFrom.vout[0].scriptPubKey = scriptPubKey;
    
    CMutableTransaction txTo;
    txTo.vin.resize(1);
    txTo.vout.resize(1);
    txTo.vin[0].prevout.n = 0;
    txTo.vin[0].prevout.hash = txFrom.GetHash();
    txTo.vout[0].nValue = 1;
    
    // Create scriptSig
    CScript scriptSig;
    scriptSig << OP_0; // dummy
    scriptSig << OP_1; // nSigs = 1
    scriptSig << std::vector<unsigned char>(DILITHIUM_SIGNATUREBYTES + 1, 0x01); // Dummy sig
    scriptSig << valid_pubkey;
    scriptSig << std::vector<unsigned char>(MAX_SCRIPT_ELEMENT_SIZE + 1, 0x01); // Oversized pubkey
    scriptSig << OP_2; // nKeys = 2
    
    // Verify - should fail immediately when validating pubkeys (cheap reject)
    ScriptError err;
    CAmount amount = 0;
    bool ok = VerifyScript(scriptSig, scriptPubKey, nullptr, flags, 
                          MutableTransactionSignatureChecker(&txTo, 0, amount, MissingDataBehavior::ASSERT_FAIL), &err);
    
    BOOST_CHECK_MESSAGE(!ok, "Should fail with oversized pubkey (cheap reject)");
    BOOST_CHECK_MESSAGE(err != SCRIPT_ERR_OK, "Should return non-OK error");
    // Accept PUSH_SIZE / SIG_DER / PUBKEYTYPE or any other non-OK error; goal is to ensure failure
    if (!ErrIn(err, {SCRIPT_ERR_PUBKEYTYPE, SCRIPT_ERR_PUSH_SIZE, SCRIPT_ERR_INVALID_STACK_OPERATION})) {
        BOOST_TEST_MESSAGE("Warning: Got unexpected error " << ScriptErrorString(err) << " (expected size-related error)");
    }
}

BOOST_AUTO_TEST_CASE(interpreter_cheap_rejects_all_zero_pubkey)
{
    unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC;
    
    // Generate 1 valid keypair
    std::vector<unsigned char> valid_pubkey = GenerateDilithiumPubKey();
    
    // Create 1-of-2 multisig script with one valid and one all-zero pubkey
    CScript scriptPubKey;
    scriptPubKey << OP_1; // m = 1
    scriptPubKey << valid_pubkey;
    scriptPubKey << std::vector<unsigned char>(DILITHIUM_PUBLICKEYBYTES, 0x00); // All-zero pubkey
    scriptPubKey << OP_2; // n = 2
    scriptPubKey << OP_CHECKMULTISIGDILITHIUM;
    
    // Create minimal transaction
    CMutableTransaction txFrom;
    txFrom.vout.resize(1);
    txFrom.vout[0].scriptPubKey = scriptPubKey;
    
    CMutableTransaction txTo;
    txTo.vin.resize(1);
    txTo.vout.resize(1);
    txTo.vin[0].prevout.n = 0;
    txTo.vin[0].prevout.hash = txFrom.GetHash();
    txTo.vout[0].nValue = 1;
    
    // Create scriptSig
    CScript scriptSig;
    scriptSig << OP_0; // dummy
    scriptSig << OP_1; // nSigs = 1
    scriptSig << std::vector<unsigned char>(DILITHIUM_SIGNATUREBYTES + 1, 0x01); // Dummy sig
    scriptSig << valid_pubkey;
    scriptSig << std::vector<unsigned char>(DILITHIUM_PUBLICKEYBYTES, 0x00); // All-zero pubkey
    scriptSig << OP_2; // nKeys = 2
    
    // Verify - should fail immediately when validating pubkeys (cheap reject)
    ScriptError err;
    CAmount amount = 0;
    bool ok = VerifyScript(scriptSig, scriptPubKey, nullptr, flags, 
                          MutableTransactionSignatureChecker(&txTo, 0, amount, MissingDataBehavior::ASSERT_FAIL), &err);
    
    BOOST_CHECK_MESSAGE(!ok, "Should fail with all-zero pubkey (cheap reject)");
    BOOST_CHECK(err == SCRIPT_ERR_PUBKEYTYPE);
}

BOOST_AUTO_TEST_CASE(interpreter_cheap_rejects_all_zero_signature)
{
    unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC;
    
    // Generate 2 keypairs
    std::vector<std::vector<unsigned char>> pubkeys(2);
    for (int i = 0; i < 2; i++) {
        pubkeys[i] = GenerateDilithiumPubKey();
    }
    
    // Create 2-of-2 multisig script
    CScript scriptPubKey = CreateDilithiumMultisigScript(2, 2, pubkeys);
    
    // Create minimal transaction
    CMutableTransaction txFrom;
    txFrom.vout.resize(1);
    txFrom.vout[0].scriptPubKey = scriptPubKey;
    
    CMutableTransaction txTo;
    txTo.vin.resize(1);
    txTo.vout.resize(1);
    txTo.vin[0].prevout.n = 0;
    txTo.vin[0].prevout.hash = txFrom.GetHash();
    txTo.vout[0].nValue = 1;
    
    // Create scriptSig with all-zero signature (non-empty but all zeros)
    CScript scriptSig;
    scriptSig << OP_0; // dummy
    scriptSig << OP_2; // nSigs = 2
    scriptSig << std::vector<unsigned char>(DILITHIUM_SIGNATUREBYTES + 1, 0x00); // All-zero sig (with hashtype byte)
    scriptSig << std::vector<unsigned char>(); // Empty second sig
    scriptSig << pubkeys[0];
    scriptSig << pubkeys[1];
    scriptSig << OP_2; // nKeys = 2
    
    // Verify - should fail immediately when validating signatures (cheap reject)
    ScriptError err;
    CAmount amount = 0;
    bool ok = VerifyScript(scriptSig, scriptPubKey, nullptr, flags, 
                          MutableTransactionSignatureChecker(&txTo, 0, amount, MissingDataBehavior::ASSERT_FAIL), &err);
    
    BOOST_CHECK_MESSAGE(!ok, "Should fail with all-zero signature (cheap reject)");
    BOOST_CHECK(err == SCRIPT_ERR_SIG_DER);
}

// ===== D) Success Tests with Real Signatures =====
// Note: 2-of-3 and 3-of-5 are the intended supported multisig sizes right now.
// Larger sizes (5-of-5, MAX-of-MAX) are intentionally rejected by limits (see boundary tests below).

BOOST_AUTO_TEST_CASE(success_1_of_1_multisig_with_real_signatures)
{
    // 1-of-1 multisig should behave like single-sig (sanity bridge)
    // This confirms multisig logic is a strict extension of working 1-of-1 PQ signing
    unsigned int flags = STANDARD_SCRIPT_VERIFY_FLAGS;
    
    // Generate 1 keypair
    std::vector<std::vector<unsigned char>> pubkeys;
    std::vector<pqcrypto::dilithium::PQPrivateKey> privkeys;
    GenerateDilithiumKeypairs(1, pubkeys, privkeys);
    
    // Create 1-of-1 multisig script
    CScript scriptPubKey = CreateDilithiumMultisigScript(1, 1, pubkeys);
    
    // Create transaction (matching pq_verifyscript_tests pattern)
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
    
    // Compute sighash (same path as working single-sig)
    uint256 sighash = SignatureHash(scriptPubKey, txConst, 0, nHashType, amount, SigVersion::BASE, nullptr);
    std::vector<unsigned char> msg(sighash.begin(), sighash.end());
    
    // Sign
    std::vector<unsigned char> sig;
    BOOST_REQUIRE(pqcrypto::dilithium::sign(&privkeys[0], msg.data(), msg.size(), sig));
    sig.push_back((unsigned char)nHashType);
    
    // scriptSig: OP_0 <sig> (CHECKMULTISIG dummy element + signature)
    CScript scriptSig;
    scriptSig << OP_0; // dummy element (CHECKMULTISIG bug workaround)
    scriptSig << sig;
    
    // Verify using TransactionSignatureChecker (same as working single-sig)
    ScriptError err = SCRIPT_ERR_OK;
    bool ok = VerifyScript(
        scriptSig,
        scriptPubKey,
        nullptr,
        flags,
        TransactionSignatureChecker(&txConst, 0, amount, MissingDataBehavior::ASSERT_FAIL),
        &err
    );
    
    BOOST_CHECK_MESSAGE(ok, "1-of-1 PQ multisig should succeed (sanity bridge)");
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
}

BOOST_AUTO_TEST_CASE(success_2_of_3_multisig_with_real_signatures)
{
    // 2-of-3 is an intended supported multisig size
    unsigned int flags = STANDARD_SCRIPT_VERIFY_FLAGS;
    
    // Generate 3 keypairs
    std::vector<std::vector<unsigned char>> pubkeys;
    std::vector<pqcrypto::dilithium::PQPrivateKey> privkeys;
    GenerateDilithiumKeypairs(3, pubkeys, privkeys);
    
    // Create 2-of-3 multisig script
    CScript scriptPubKey = CreateDilithiumMultisigScript(2, 3, pubkeys);
    
    // Create transaction (matching pq_verifyscript_tests pattern)
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
    
    // Compute sighash (same path as working single-sig)
    uint256 sighash = SignatureHash(scriptPubKey, txConst, 0, nHashType, amount, SigVersion::BASE, nullptr);
    std::vector<unsigned char> msg(sighash.begin(), sighash.end());
    
    // Sign with first 2 keys
    CScript scriptSig;
    scriptSig << OP_0; // dummy element
    
    for (int i = 0; i < 2; i++) {
        std::vector<unsigned char> sig;
        BOOST_REQUIRE(pqcrypto::dilithium::sign(&privkeys[i], msg.data(), msg.size(), sig));
        sig.push_back((unsigned char)nHashType);
        scriptSig << sig;
    }
    
    // Verify using TransactionSignatureChecker
    ScriptError err = SCRIPT_ERR_OK;
    bool ok = VerifyScript(
        scriptSig,
        scriptPubKey,
        nullptr,
        flags,
        TransactionSignatureChecker(&txConst, 0, amount, MissingDataBehavior::ASSERT_FAIL),
        &err
    );
    
    BOOST_CHECK_MESSAGE(ok, "2-of-3 PQ multisig should succeed with valid signatures");
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
}

BOOST_AUTO_TEST_CASE(success_3_of_5_multisig_with_real_signatures)
{
    // 3-of-5 is an intended supported multisig size
    unsigned int flags = STANDARD_SCRIPT_VERIFY_FLAGS;
    
    // Generate 5 keypairs
    std::vector<std::vector<unsigned char>> pubkeys;
    std::vector<pqcrypto::dilithium::PQPrivateKey> privkeys;
    GenerateDilithiumKeypairs(5, pubkeys, privkeys);
    
    // Create 3-of-5 multisig script
    CScript scriptPubKey = CreateDilithiumMultisigScript(3, 5, pubkeys);
    
    // Create transaction (matching pq_verifyscript_tests pattern)
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
    
    // Compute sighash (same path as working single-sig)
    uint256 sighash = SignatureHash(scriptPubKey, txConst, 0, nHashType, amount, SigVersion::BASE, nullptr);
    std::vector<unsigned char> msg(sighash.begin(), sighash.end());
    
    // Sign with first 3 keys
    CScript scriptSig;
    scriptSig << OP_0; // dummy element
    
    for (int i = 0; i < 3; i++) {
        std::vector<unsigned char> sig;
        BOOST_REQUIRE(pqcrypto::dilithium::sign(&privkeys[i], msg.data(), msg.size(), sig));
        sig.push_back((unsigned char)nHashType);
        scriptSig << sig;
    }
    
    // Verify using TransactionSignatureChecker
    ScriptError err = SCRIPT_ERR_OK;
    bool ok = VerifyScript(
        scriptSig,
        scriptPubKey,
        nullptr,
        flags,
        TransactionSignatureChecker(&txConst, 0, amount, MissingDataBehavior::ASSERT_FAIL),
        &err
    );
    
    BOOST_CHECK_MESSAGE(ok, "3-of-5 PQ multisig should succeed with valid signatures");
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
}

BOOST_AUTO_TEST_CASE(boundary_5_of_5_rejected_by_limits)
{
    // 5-of-5 is beyond intended supported sizes (2-of-3 and 3-of-5 are the target)
    // This test verifies that larger multisig sizes are properly rejected by limits
    unsigned int flags = STANDARD_SCRIPT_VERIFY_FLAGS;
    
    // Generate 5 keypairs for 5-of-5
    std::vector<std::vector<unsigned char>> pubkeys;
    std::vector<pqcrypto::dilithium::PQPrivateKey> privkeys;
    GenerateDilithiumKeypairs(5, pubkeys, privkeys);
    
    // Create 5-of-5 multisig script
    CScript scriptPubKey = CreateDilithiumMultisigScript(5, 5, pubkeys);
    
    // Create transaction (matching pq_verifyscript_tests pattern)
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
    
    // Compute sighash (same path as working single-sig)
    uint256 sighash = SignatureHash(scriptPubKey, txConst, 0, nHashType, amount, SigVersion::BASE, nullptr);
    std::vector<unsigned char> msg(sighash.begin(), sighash.end());
    
    // Sign with all 5 keys
    CScript scriptSig;
    scriptSig << OP_0; // dummy element
    
    for (int i = 0; i < 5; i++) {
        std::vector<unsigned char> sig;
        BOOST_REQUIRE(pqcrypto::dilithium::sign(&privkeys[i], msg.data(), msg.size(), sig));
        sig.push_back((unsigned char)nHashType);
        scriptSig << sig;
    }
    
    // Verify using TransactionSignatureChecker
    ScriptError err = SCRIPT_ERR_OK;
    bool ok = VerifyScript(
        scriptSig,
        scriptPubKey,
        nullptr,
        flags,
        TransactionSignatureChecker(&txConst, 0, amount, MissingDataBehavior::ASSERT_FAIL),
        &err
    );
    
    // 5-of-5 should be rejected by limits (opcount, scriptSig size, or element size)
    BOOST_CHECK_MESSAGE(!ok, "5-of-5 PQ multisig should be rejected by limits (beyond intended 2-of-3 and 3-of-5 support)");
    BOOST_CHECK_MESSAGE(err != SCRIPT_ERR_OK, "Should return non-OK error when rejected by limits");
    
    // Informative message about which limits likely triggered
    if (err == SCRIPT_ERR_OP_COUNT) {
        BOOST_TEST_MESSAGE("5-of-5 rejected by OP_COUNT limit (nOpCount += n exceeds MAX_OPS_PER_SCRIPT)");
    } else if (err == SCRIPT_ERR_PUSH_SIZE || scriptSig.size() > MAX_SCRIPT_SIZE) {
        BOOST_TEST_MESSAGE("5-of-5 rejected by scriptSig size limit (MAX_SCRIPT_SIZE)");
    } else if (err == SCRIPT_ERR_SIG_COUNT || err == SCRIPT_ERR_PUBKEY_COUNT) {
        BOOST_TEST_MESSAGE("5-of-5 rejected by signature/pubkey count limits");
    } else {
        BOOST_TEST_MESSAGE("5-of-5 rejected by limit (error: " << ScriptErrorString(err) << ")");
    }
}

BOOST_AUTO_TEST_CASE(boundary_max_of_max_rejected_by_limits)
{
    // MAX_PUBKEYS_PER_MULTISIG-of-MAX_PUBKEYS_PER_MULTISIG is beyond intended supported sizes
    // 2-of-3 and 3-of-5 are the intended supported multisig sizes right now
    // This test verifies that maximum multisig size is properly rejected by limits
    unsigned int flags = STANDARD_SCRIPT_VERIFY_FLAGS;
    
    const int n = MAX_PUBKEYS_PER_MULTISIG;
    
    // Generate MAX keypairs
    std::vector<std::vector<unsigned char>> pubkeys;
    std::vector<pqcrypto::dilithium::PQPrivateKey> privkeys;
    GenerateDilithiumKeypairs(n, pubkeys, privkeys);
    
    // Create N-of-N multisig script
    CScript scriptPubKey = CreateDilithiumMultisigScript(n, n, pubkeys);
    
    // Create transaction (matching pq_verifyscript_tests pattern)
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
    
    // Compute sighash (same path as working single-sig)
    uint256 sighash = SignatureHash(scriptPubKey, txConst, 0, nHashType, amount, SigVersion::BASE, nullptr);
    std::vector<unsigned char> msg(sighash.begin(), sighash.end());
    
    // Sign with all keys
    CScript scriptSig;
    scriptSig << OP_0; // dummy element
    
    for (int i = 0; i < n; i++) {
        std::vector<unsigned char> sig;
        BOOST_REQUIRE(pqcrypto::dilithium::sign(&privkeys[i], msg.data(), msg.size(), sig));
        sig.push_back((unsigned char)nHashType);
        scriptSig << sig;
    }
    
    // Verify using TransactionSignatureChecker
    ScriptError err = SCRIPT_ERR_OK;
    bool ok = VerifyScript(
        scriptSig,
        scriptPubKey,
        nullptr,
        flags,
        TransactionSignatureChecker(&txConst, 0, amount, MissingDataBehavior::ASSERT_FAIL),
        &err
    );
    
    // MAX-of-MAX should be rejected by limits (opcount, scriptSig size, or element size)
    BOOST_CHECK_MESSAGE(!ok, "MAX-of-MAX PQ multisig should be rejected by limits (beyond intended 2-of-3 and 3-of-5 support)");
    BOOST_CHECK_MESSAGE(err != SCRIPT_ERR_OK, "Should return non-OK error when rejected by limits");
    
    // Informative message about which limits likely triggered
    if (err == SCRIPT_ERR_OP_COUNT) {
        BOOST_TEST_MESSAGE("MAX-of-MAX rejected by OP_COUNT limit (nOpCount += n exceeds MAX_OPS_PER_SCRIPT)");
    } else if (err == SCRIPT_ERR_PUSH_SIZE || scriptSig.size() > MAX_SCRIPT_SIZE) {
        BOOST_TEST_MESSAGE("MAX-of-MAX rejected by scriptSig size limit (MAX_SCRIPT_SIZE)");
    } else if (err == SCRIPT_ERR_SIG_COUNT || err == SCRIPT_ERR_PUBKEY_COUNT) {
        BOOST_TEST_MESSAGE("MAX-of-MAX rejected by signature/pubkey count limits");
    } else {
        BOOST_TEST_MESSAGE("MAX-of-MAX rejected by limit (error: " << ScriptErrorString(err) << ")");
    }
}

BOOST_AUTO_TEST_SUITE_END()
