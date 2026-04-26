#include <boost/test/unit_test.hpp>

#include <consensus/consensus.h>
#include <policy/policy.h>
#include <script/interpreter.h>

BOOST_AUTO_TEST_CASE(pq_witness_activation_height_constant)
{
    BOOST_CHECK_EQUAL(Consensus::PQ_WITNESS_ACTIVATION_HEIGHT, 230000);
}

BOOST_AUTO_TEST_CASE(script_verify_pq_witness_flag_distinct)
{
    BOOST_CHECK((STANDARD_SCRIPT_VERIFY_FLAGS & SCRIPT_VERIFY_PQ_WITNESS) == 0);
    BOOST_CHECK_NE(SCRIPT_VERIFY_PQ_WITNESS, SCRIPT_VERIFY_NONE);
}
