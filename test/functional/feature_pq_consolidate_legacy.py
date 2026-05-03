#!/usr/bin/env python3
# Copyright (c) 2026 The Q-BitX Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""pqconsolidatelegacy: pre-activation legacy PQ UTXO batching (regtest)."""

from test_framework.blocktools import COINBASE_MATURITY
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than_or_equal


def count_utxos_for_address(wallet_rpc, addr):
    return len([u for u in wallet_rpc.listunspent() if u.get("address") == addr])


class PqConsolidateLegacyTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-testactivationheight=pq_witness@100000"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def add_options(self, parser):
        self.add_wallet_options(parser)

    def run_test(self):
        node = self.nodes[0]
        node.createwallet(
            wallet_name="consolidate",
            descriptors=self.options.descriptors,
            load_on_startup=True,
        )
        w = node.get_wallet_rpc("consolidate")

        pq_addr = w.getnewaddress("", "pq")
        assert not pq_addr.startswith("rdil1"), "pq_witness far in future: getnewaddress pq must be legacy"

        self.log.info("Fund wallet and mine maturity chain")
        self.generatetoaddress(node, COINBASE_MATURITY + 1, w.getnewaddress())

        leg = w.getnewlegacypqaddress()
        self.log.info("Mine 4 coinbases to legacy PQ address")
        for _ in range(4):
            node.generatetoaddress(1, leg)

        self.generatetoaddress(node, COINBASE_MATURITY, w.getnewaddress())

        empty = w.getnewlegacypqaddress()
        r_empty = w.pqconsolidatelegacy(empty, "low", 16, True, 1)
        assert_equal(r_empty["status"], "nothing_to_consolidate")

        self.log.info("dry_run plan: max_inputs=16 (clamped cap)")
        r = w.pqconsolidatelegacy(leg, "low", 16, True, 1)
        assert_equal(r["status"], "ok")
        assert_equal(r["dry_run"], True)
        assert_equal(r["mode"], "legacy_pq_consolidation")
        assert_equal(r["max_inputs"], 16)
        assert_greater_than_or_equal(r["candidate_utxo_count"], 4)
        assert_greater_than_or_equal(r["planned_batches"], 1)
        d0 = r["details"][0]
        assert_equal(d0["inputs_used"] <= 16, True)
        assert_equal(d0["input_amount"] - d0["fee"], d0["output_amount"])
        assert d0["to_address"] is None
        assert_equal(d0["to_address_policy"], "fresh_legacy_address_created_on_broadcast")
        assert "rawtx" not in d0
        assert d0.get("txid") in (None, "")

        n_from_before = count_utxos_for_address(w, leg)
        assert_equal(n_from_before, 4)

        self.log.info("broadcast one consolidation batch")
        r2 = w.pqconsolidatelegacy(leg, "low", 16, False, 1)
        assert_equal(r2["dry_run"], False)
        assert_equal(r2["created_tx_count"], 1)
        assert_equal(len(r2["txids"]), 1)
        d0b = r2["details"][0]
        assert_equal(d0b["inputs_used"] <= 16, True)
        out_addr = d0b["to_address"]
        assert out_addr is not None
        assert out_addr != leg
        assert not out_addr.startswith("rdil1")
        assert "to_address_policy" not in d0b

        txid = r2["txids"][0]
        dec = node.getrawtransaction(txid, True)
        assert_equal(len(dec["vout"]), 1)
        assert_equal(dec["vout"][0]["scriptPubKey"]["address"], out_addr)

        node.generate(1)

        n_from_after = count_utxos_for_address(w, leg)
        assert_equal(n_from_after, 0)
        assert_equal(count_utxos_for_address(w, out_addr), 1)

        self.log.info("nothing left at from_address after full consolidation")
        r_left = w.pqconsolidatelegacy(leg, "low", 16, True, 1)
        assert_equal(r_left["status"], "nothing_to_consolidate")

        self.log.info("repeat dry_run on funded legacy address (positional args)")
        leg2 = w.getnewlegacypqaddress()
        for _ in range(2):
            node.generatetoaddress(1, leg2)
        self.generatetoaddress(node, COINBASE_MATURITY, w.getnewaddress())
        r3 = w.pqconsolidatelegacy(leg2, "low", 16, True, 1)
        assert_equal(r3["status"], "ok")


if __name__ == "__main__":
    PqConsolidateLegacyTest(__file__).main()
