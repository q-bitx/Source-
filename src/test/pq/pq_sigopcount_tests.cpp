// PQ / Dilithium sigop counting (CScript::GetSigOpCount only).
// Pipeline: src/test/pq/pq_sigop_pipeline_tests.cpp; Core-style witness cases: src/test/sigopcount_tests.cpp (GetTxSigOpCost).

#include <script/script.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(pq_sigopcount_tests)

BOOST_AUTO_TEST_CASE(pqchecksig_counts_one)
{
    CScript s;
    s << OP_PQCHECKSIG;
    BOOST_CHECK_EQUAL(s.GetSigOpCount(true, true), 1U);
    BOOST_CHECK_EQUAL(s.GetSigOpCount(false, true), 1U);
    BOOST_CHECK_EQUAL(s.GetSigOpCount(true, false), 0U);
}

BOOST_AUTO_TEST_CASE(checksigdilithium_counts_one)
{
    CScript s;
    s << OP_CHECKSIGDILITHIUM;
    BOOST_CHECK_EQUAL(s.GetSigOpCount(true, true), 1U);
    BOOST_CHECK_EQUAL(s.GetSigOpCount(false, true), 1U);

    CScript s_verify;
    s_verify << OP_CHECKSIGDILITHIUMVERIFY;
    BOOST_CHECK_EQUAL(s_verify.GetSigOpCount(true, true), 1U);
}

BOOST_AUTO_TEST_CASE(checkmultisigdilithium_accurate_uses_op_prefix)
{
    CScript s;
    s << OP_2 << OP_3 << OP_CHECKMULTISIGDILITHIUM;
    BOOST_CHECK_EQUAL(s.GetSigOpCount(true, true), 3U);
}

BOOST_AUTO_TEST_CASE(checkmultisigdilithium_inaccurate_uses_max_keys)
{
    CScript s;
    s << OP_CHECKMULTISIGDILITHIUM;
    BOOST_CHECK_EQUAL(s.GetSigOpCount(false, true), static_cast<unsigned int>(MAX_PUBKEYS_PER_MULTISIG));
}

BOOST_AUTO_TEST_CASE(mixed_pq_and_dilithium_multisig)
{
    CScript s;
    s << OP_PQCHECKSIG << OP_CHECKSIGDILITHIUM << OP_1 << OP_2 << OP_CHECKMULTISIGDILITHIUMVERIFY;
    BOOST_CHECK_EQUAL(s.GetSigOpCount(true, true), 1U + 1U + 2U);
}

BOOST_AUTO_TEST_CASE(legacy_checksig_always_counts_with_or_without_pq_sigops)
{
    CScript s;
    s << OP_CHECKSIG << OP_CHECKSIGDILITHIUM;
    BOOST_CHECK_EQUAL(s.GetSigOpCount(true, false), 1U);
    BOOST_CHECK_EQUAL(s.GetSigOpCount(true, true), 2U);
}

BOOST_AUTO_TEST_CASE(pqchecksigadd_counts_one_when_pq_sigops_active)
{
    CScript s;
    s << OP_PQCHECKSIGADD;
    BOOST_CHECK_EQUAL(s.GetSigOpCount(true, true), 1U);
    BOOST_CHECK_EQUAL(s.GetSigOpCount(false, true), 1U);
    BOOST_CHECK_EQUAL(s.GetSigOpCount(true, false), 0U);
}

BOOST_AUTO_TEST_SUITE_END()
