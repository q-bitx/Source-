#include <boost/test/unit_test.hpp>

#include <consensus/consensus.h>
#include <consensus/params.h>
#include <consensus/validation.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>

BOOST_AUTO_TEST_CASE(pq_witness_consensus_params_height_and_constants)
{
    Consensus::Params p{};
    p.nPQWitnessHeight = 230000;

    BOOST_CHECK(!Consensus::IsPQWitnessEnabled(p, 229999));
    BOOST_CHECK(Consensus::IsPQWitnessEnabled(p, 230000));
    BOOST_CHECK(Consensus::IsPQWitnessEnabled(p, 230001));

    BOOST_CHECK_EQUAL(GetWitnessDiscountScale(p, 229999), WITNESS_SCALE_FACTOR);
    BOOST_CHECK_EQUAL(GetWitnessDiscountScale(p, 230000), PQ_WITNESS_SCALE_FACTOR);

    BOOST_CHECK_EQUAL(WITNESS_SCALE_FACTOR, 4);
    BOOST_CHECK_EQUAL(PQ_WITNESS_SCALE_FACTOR, 16);
    BOOST_CHECK_EQUAL(LEGACY_MAX_BLOCK_P2P_SERIALIZED_SIZE, 4000000U);
    BOOST_CHECK_EQUAL(QBX_MAX_BLOCK_SERIALIZED_SIZE, 8000000U);
    BOOST_CHECK_EQUAL(LEGACY_MAX_BLOCK_WEIGHT, 16000000U);
    BOOST_CHECK_EQUAL(QBX_MAX_BLOCK_WEIGHT, 8000000U);
    BOOST_CHECK_EQUAL(MAX_BLOCK_DISK_SERIALIZED_SIZE, LEGACY_MAX_BLOCK_WEIGHT);
}

BOOST_AUTO_TEST_CASE(weight_uses_k16_after_activation_for_same_tx)
{
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    CTransaction tx(mtx);
    const int32_t w4 = GetTransactionWeightWithScale(tx, WITNESS_SCALE_FACTOR);
    const int32_t w16 = GetTransactionWeightWithScale(tx, PQ_WITNESS_SCALE_FACTOR);
    BOOST_CHECK_GT(w16, w4);
}

BOOST_AUTO_TEST_CASE(script_verify_pq_witness_flag_distinct)
{
    BOOST_CHECK((STANDARD_SCRIPT_VERIFY_FLAGS & SCRIPT_VERIFY_PQ_WITNESS) == 0);
    BOOST_CHECK_NE(SCRIPT_VERIFY_PQ_WITNESS, SCRIPT_VERIFY_NONE);
}

BOOST_AUTO_TEST_CASE(witness_discount_scale_change_detected_across_activation_reorg_boundary)
{
    Consensus::Params p{};
    p.nPQWitnessHeight = 230000;

    BOOST_CHECK(IsWitnessDiscountScaleChangedAcrossTips(p, 229998, 229999)); // next: 229999 -> 230000
    BOOST_CHECK(IsWitnessDiscountScaleChangedAcrossTips(p, 229999, 229998)); // next: 230000 -> 229999
    BOOST_CHECK(!IsWitnessDiscountScaleChangedAcrossTips(p, 229997, 229998));
    BOOST_CHECK(!IsWitnessDiscountScaleChangedAcrossTips(p, 230000, 230001));
}

BOOST_AUTO_TEST_CASE(pq_sigops_activation_change_detected_across_activation_reorg_boundary)
{
    Consensus::Params p{};
    p.nPQSigopsHeight = 230000;

    // Crossing forward: next height goes from 229999 -> 230000
    BOOST_CHECK(IsPQSigopsActivationChangedAcrossTips(p, 229998, 229999));
    // Crossing backward
    BOOST_CHECK(IsPQSigopsActivationChangedAcrossTips(p, 229999, 229998));

    // Non-crossing before
    BOOST_CHECK(!IsPQSigopsActivationChangedAcrossTips(p, 229997, 229998));
    // Non-crossing after
    BOOST_CHECK(!IsPQSigopsActivationChangedAcrossTips(p, 230000, 230001));
}
