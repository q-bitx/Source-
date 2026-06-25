// PQ sighash semantics: SIGHASH_ALL-only before SCRIPT_VERIFY_PQ_WITNESS; Bitcoin-style after.

#include <boost/test/unit_test.hpp>

#include <crypto/dilithium.h>
#include <crypto/dilithium_wrapper.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <util/transaction_identifier.h>

namespace {
std::vector<unsigned char> ToVec(const uint8_t* p, size_t n)
{
    return std::vector<unsigned char>(p, p + n);
}

struct PqKeypair {
    pqcrypto::dilithium::PQPublicKey pub{};
    pqcrypto::dilithium::PQPrivateKey priv{};
    std::vector<unsigned char> vch_pub;

    PqKeypair()
    {
        pqcrypto::dilithium::keygen(&pub, &priv);
        vch_pub = ToVec(pub.data, DILITHIUM_PUBLICKEYBYTES);
    }
};

CMutableTransaction MakeSpendTx(const CScript& script_pub_key, const CAmount amount)
{
    CMutableTransaction tx;
    tx.version = 1;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vin[0].prevout = COutPoint(Txid::FromUint256(uint256::ZERO), 0);
    tx.vin[0].scriptSig = CScript();
    tx.vout[0].nValue = amount - 1000;
    tx.vout[0].scriptPubKey = CScript() << OP_TRUE;
    return tx;
}

std::vector<unsigned char> SignDilithium(const pqcrypto::dilithium::PQPrivateKey& priv,
                                         const CScript& script_code,
                                         const CTransaction& tx,
                                         int n_hash_type,
                                         CAmount amount,
                                         SigVersion sigversion)
{
    const uint256 sighash = SignatureHash(script_code, tx, 0, n_hash_type, amount, sigversion, nullptr);
    std::vector<unsigned char> msg(sighash.begin(), sighash.end());
    std::vector<unsigned char> sig;
    BOOST_REQUIRE(pqcrypto::dilithium::sign(&priv, msg.data(), msg.size(), sig));
    sig.push_back(static_cast<unsigned char>(n_hash_type));
    return sig;
}
} // namespace

BOOST_AUTO_TEST_SUITE(pq_sighash_consistency_tests)

BOOST_AUTO_TEST_CASE(pre_activation_pqchecksig_rejects_sighash_none)
{
    const PqKeypair keys;
    const CScript script_pub_key = CScript() << keys.vch_pub << OP_PQCHECKSIG;
    const CAmount amount = 1 * COIN;
    CMutableTransaction mtx = MakeSpendTx(script_pub_key, amount);
    const CTransaction tx(mtx);

    const std::vector<unsigned char> sig = SignDilithium(keys.priv, script_pub_key, tx, SIGHASH_NONE, amount, SigVersion::BASE);
    mtx.vin[0].scriptSig = CScript() << sig;

    ScriptError err = SCRIPT_ERR_OK;
    const unsigned int flags = STANDARD_SCRIPT_VERIFY_FLAGS; // no SCRIPT_VERIFY_PQ_WITNESS
    const bool ok = VerifyScript(
        mtx.vin[0].scriptSig,
        script_pub_key,
        nullptr,
        flags,
        TransactionSignatureChecker(&tx, 0, amount, MissingDataBehavior::ASSERT_FAIL),
        &err);

    BOOST_CHECK(!ok);
    // CheckPQSignature rejects before verify; script evaluation fails (typically EVAL_FALSE).
    BOOST_CHECK(err == SCRIPT_ERR_EVAL_FALSE || err == SCRIPT_ERR_SIG_HASHTYPE);
}

BOOST_AUTO_TEST_CASE(post_activation_pqchecksig_accepts_sighash_none)
{
    const PqKeypair keys;
    const CScript script_pub_key = CScript() << keys.vch_pub << OP_PQCHECKSIG;
    const CAmount amount = 1 * COIN;
    CMutableTransaction mtx = MakeSpendTx(script_pub_key, amount);
    const CTransaction tx(mtx);

    const std::vector<unsigned char> sig = SignDilithium(keys.priv, script_pub_key, tx, SIGHASH_NONE, amount, SigVersion::BASE);
    mtx.vin[0].scriptSig = CScript() << sig;

    ScriptError err = SCRIPT_ERR_OK;
    const unsigned int flags = STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_PQ_WITNESS;
    const bool ok = VerifyScript(
        mtx.vin[0].scriptSig,
        script_pub_key,
        nullptr,
        flags,
        TransactionSignatureChecker(&tx, 0, amount, MissingDataBehavior::ASSERT_FAIL),
        &err);

    BOOST_CHECK_MESSAGE(ok, ScriptErrorString(err));
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
}

BOOST_AUTO_TEST_CASE(post_activation_pqchecksigadd_accepts_sighash_none)
{
    const PqKeypair keys;
    const CAmount amount = 1000;
    CMutableTransaction mtx = MakeSpendTx(CScript() << OP_TRUE, amount);
    const CTransaction tx(mtx);

    // scriptCode for sighash omits the signature (removed via FindAndDelete during verify).
    CScript script_code;
    script_code << CScriptNum(5).getvch() << keys.vch_pub << OP_PQCHECKSIGADD;
    const std::vector<unsigned char> sig = SignDilithium(keys.priv, script_code, tx, SIGHASH_NONE, amount, SigVersion::BASE);

    CScript script;
    script << CScriptNum(5).getvch() << sig << keys.vch_pub << OP_PQCHECKSIGADD;

    std::vector<std::vector<unsigned char>> stack;
    ScriptExecutionData execdata;
    ScriptError err = SCRIPT_ERR_OK;
    const unsigned int flags = SCRIPT_VERIFY_PQ_WITNESS | SCRIPT_VERIFY_STRICTENC;

    const bool ok = EvalScript(
        stack,
        script,
        flags,
        TransactionSignatureChecker(&tx, 0, amount, MissingDataBehavior::ASSERT_FAIL),
        SigVersion::BASE,
        execdata,
        &err);

    BOOST_CHECK_MESSAGE(ok, ScriptErrorString(err));
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
    BOOST_REQUIRE_EQUAL(stack.size(), 1U);
    BOOST_CHECK_EQUAL(CScriptNum(stack.back(), false).getint(), 6);
}

BOOST_AUTO_TEST_CASE(post_activation_checksigdilithium_matches_pqchecksig_sighash_none)
{
    const PqKeypair keys;
    const CAmount amount = 1 * COIN;
    const unsigned int flags = STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_PQ_WITNESS;

    const CScript pq_script = CScript() << keys.vch_pub << OP_PQCHECKSIG;
    CMutableTransaction pq_mtx = MakeSpendTx(pq_script, amount);
    const CTransaction pq_tx(pq_mtx);
    const std::vector<unsigned char> pq_sig = SignDilithium(keys.priv, pq_script, pq_tx, SIGHASH_NONE, amount, SigVersion::BASE);
    pq_mtx.vin[0].scriptSig = CScript() << pq_sig;

    const CScript dil_script = CScript() << keys.vch_pub << OP_CHECKSIGDILITHIUM;
    CMutableTransaction dil_mtx = MakeSpendTx(dil_script, amount);
    const CTransaction dil_tx(dil_mtx);
    const std::vector<unsigned char> dil_sig = SignDilithium(keys.priv, dil_script, dil_tx, SIGHASH_NONE, amount, SigVersion::BASE);
    dil_mtx.vin[0].scriptSig = CScript() << dil_sig;

    ScriptError err = SCRIPT_ERR_OK;
    const bool pq_ok = VerifyScript(
        pq_mtx.vin[0].scriptSig,
        pq_script,
        nullptr,
        flags,
        TransactionSignatureChecker(&pq_tx, 0, amount, MissingDataBehavior::ASSERT_FAIL),
        &err);
    BOOST_CHECK_MESSAGE(pq_ok, ScriptErrorString(err));

    err = SCRIPT_ERR_OK;
    const bool dil_ok = VerifyScript(
        dil_mtx.vin[0].scriptSig,
        dil_script,
        nullptr,
        flags,
        TransactionSignatureChecker(&dil_tx, 0, amount, MissingDataBehavior::ASSERT_FAIL),
        &err);
    BOOST_CHECK_MESSAGE(dil_ok, ScriptErrorString(err));
}

BOOST_AUTO_TEST_SUITE_END()
