// Copyright (c) 2026 The QBitX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <coins.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/tx_verify.h>
#include <policy/policy.h>
#include <script/script.h>

#include <boost/test/unit_test.hpp>

#include <vector>

namespace {
static void BuildTxs(CMutableTransaction& spending_tx,
                     CCoinsViewCache& coins,
                     CMutableTransaction& creation_tx,
                     const CScript& script_pub_key,
                     const CScript& script_sig,
                     const CScriptWitness& witness)
{
    creation_tx.version = 1;
    creation_tx.vin.resize(1);
    creation_tx.vin[0].prevout.SetNull();
    creation_tx.vin[0].scriptSig = CScript();
    creation_tx.vout.resize(1);
    creation_tx.vout[0].nValue = 1;
    creation_tx.vout[0].scriptPubKey = script_pub_key;

    spending_tx.version = 1;
    spending_tx.vin.resize(1);
    spending_tx.vin[0].prevout.hash = creation_tx.GetHash();
    spending_tx.vin[0].prevout.n = 0;
    spending_tx.vin[0].scriptSig = script_sig;
    spending_tx.vin[0].scriptWitness = witness;
    spending_tx.vout.resize(1);
    spending_tx.vout[0].nValue = 1;
    spending_tx.vout[0].scriptPubKey = CScript();

    AddCoins(coins, CTransaction(creation_tx), 0);
}
} // namespace

BOOST_AUTO_TEST_SUITE(pq_sigop_pipeline_tests)

BOOST_AUTO_TEST_CASE(gettransactionsigopcost_counts_dilithium_paths)
{
    CMutableTransaction creation_tx;
    CMutableTransaction spending_tx;
    CCoinsView coins_dummy;
    CCoinsViewCache coins(&coins_dummy);
    const uint32_t flags{SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_P2SH};

    // A) Output script with OP_CHECKSIGDILITHIUM contributes one legacy sigop.
    {
        std::vector<unsigned char> dilithium_pubkey(32, 1);
        CScript script_pub_key = CScript() << dilithium_pubkey << OP_CHECKSIGDILITHIUM;
        BuildTxs(spending_tx, coins, creation_tx, script_pub_key, CScript(), CScriptWitness());

        BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(creation_tx), coins, flags, true), WITNESS_SCALE_FACTOR);
        BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(spending_tx), coins, flags, true), 0);
        BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(creation_tx), coins, flags, false), 0);
    }

    // B) Accurate OP_N counting for OP_CHECKMULTISIGDILITHIUM in P2WSH witness script.
    {
        CScript witness_script = CScript() << OP_2 << OP_3 << OP_CHECKMULTISIGDILITHIUM;
        CScript script_pub_key = GetScriptForDestination(WitnessV0ScriptHash(witness_script));
        CScriptWitness witness;
        witness.stack.emplace_back(0);
        witness.stack.emplace_back(0);
        witness.stack.emplace_back(witness_script.begin(), witness_script.end());
        BuildTxs(spending_tx, coins, creation_tx, script_pub_key, CScript(), witness);

        BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(spending_tx), coins, flags, true), 3);
        BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(spending_tx), coins, flags, false), 0);
    }

    // C) Fallback non-accurate path for legacy counting uses MAX_PUBKEYS_PER_MULTISIG.
    {
        CScript script_pub_key = CScript() << OP_CHECKMULTISIGDILITHIUM;
        BuildTxs(spending_tx, coins, creation_tx, script_pub_key, CScript(), CScriptWitness());

        BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(creation_tx), coins, flags, true),
                          MAX_PUBKEYS_PER_MULTISIG * WITNESS_SCALE_FACTOR);
        BOOST_CHECK_EQUAL(GetTransactionSigOpCost(CTransaction(creation_tx), coins, flags, false), 0);
    }
}

BOOST_AUTO_TEST_CASE(pq_sigops_pre_post_activation_cost)
{
    // Many outputs with bare OP_CHECKMULTISIGDILITHIUM: counted only when PQ sigops are active.
    CMutableTransaction fund;
    fund.vin.resize(1);
    fund.vin[0].prevout.SetNull();
    fund.vout.resize(1);
    fund.vout[0].nValue = 50 * COIN;
    fund.vout[0].scriptPubKey = CScript() << OP_TRUE;

    CCoinsView coins_dummy;
    CCoinsViewCache coins(&coins_dummy);
    AddCoins(coins, CTransaction(fund), 1);

    constexpr CAmount out_value{1000000}; // CENT
    const int sigops_per_output = MAX_PUBKEYS_PER_MULTISIG * WITNESS_SCALE_FACTOR;
    const int outputs_needed = (MAX_STANDARD_TX_SIGOPS_COST / sigops_per_output) + 1;

    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout = COutPoint(fund.GetHash(), 0);
    tx.vin[0].scriptSig = CScript();
    for (int i = 0; i < outputs_needed; ++i) {
        tx.vout.emplace_back(out_value, CScript() << OP_CHECKMULTISIGDILITHIUM);
    }

    const uint32_t flags{SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_P2SH};
    const int64_t cost_active{GetTransactionSigOpCost(CTransaction(tx), coins, flags, true)};
    const int64_t cost_inactive{GetTransactionSigOpCost(CTransaction(tx), coins, flags, false)};
    BOOST_CHECK(cost_active > MAX_STANDARD_TX_SIGOPS_COST);
    BOOST_CHECK(cost_inactive <= MAX_STANDARD_TX_SIGOPS_COST);
}

BOOST_AUTO_TEST_SUITE_END()
