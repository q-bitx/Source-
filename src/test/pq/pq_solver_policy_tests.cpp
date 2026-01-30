#include <boost/test/unit_test.hpp>

#include <policy/policy.h>
#include <script/script.h>
#include <script/solver.h>
#include "script/interpreter.h"
#include "script/standard.h"

#include "crypto/dilithium.h"

BOOST_AUTO_TEST_SUITE(pq_solver_policy_tests)

BOOST_AUTO_TEST_CASE(qbitx_solver_detects_qbitx_dilithium)
{
    std::vector<unsigned char> pub(DILITHIUM_PUBLICKEYBYTES, 1);

    CScript spk;
    spk << OP_QBITX << pub << OP_PQCHECKSIG;

    std::vector<std::vector<unsigned char>> sols;
    TxoutType t = Solver(spk, sols);

    BOOST_CHECK(t == TxoutType::QBITX_DILITHIUM);
    BOOST_REQUIRE_EQUAL(sols.size(), 1U);
    BOOST_CHECK_EQUAL(sols[0].size(), DILITHIUM_PUBLICKEYBYTES);

}

BOOST_AUTO_TEST_CASE(qbitx_script_is_standard_policy)
{
    CScript spk;
    std::vector<unsigned char> pub(DILITHIUM_PUBLICKEYBYTES, 1);

    // OP_QBITX <push(pub)> OP_PQCHECKSIG
    spk << OP_QBITX << pub << OP_PQCHECKSIG;

    std::vector<std::vector<unsigned char>> sols;
    const TxoutType typ = Solver(spk, sols);

    BOOST_CHECK(typ == TxoutType::QBITX_DILITHIUM);
    BOOST_REQUIRE_EQUAL(sols.size(), 1U);
    BOOST_CHECK_EQUAL(sols[0].size(), DILITHIUM_PUBLICKEYBYTES);
}

BOOST_AUTO_TEST_SUITE_END()
