#!/usr/bin/env python3
# Copyright (c) 2026 The Q-BitX Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Wallet RPC pqmigratelegacy. Isolated regtest only."""

import time
from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than


class PqMigrateLegacyTest(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            "-conf=bitcoin.conf",
        ]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def mine(self, count, address):
        node = self.nodes[0]
        for _ in range(count):
            self.mock_time += 150
            node.setmocktime(self.mock_time)
            self.generatetoaddress(node, 1, address)

    def run_test(self):
        node = self.nodes[0]
        self.mock_time = int(time.time())
        legacy = node.getnewaddress("", "dilithium-legacy")
        self.mine(130, legacy)
        native = node.getnewaddress("", "dilithium-bech32")
        assert native.startswith("rdil1")
        before = set(node.getrawmempool())

        # Dilithium destinations use placeholder managers with no descriptor
        # string, so listdescriptors throws. A new destination would still
        # appear in the address list.
        addrs_before = set(node.getaddressesbylabel(""))
        empty_plan = node.pqmigratelegacy("all", "", 1, 1600000, "normal", True, False)
        assert_equal(empty_plan["dry_run"], True)
        assert_equal(empty_plan["destination_address"], "")
        assert_equal(empty_plan["transactions"], [])
        assert_equal(set(node.getaddressesbylabel("")), addrs_before)

        plan = node.pqmigratelegacy(legacy, native, 10, 1600000, "normal", True, False)
        assert_equal(plan["dry_run"], True)
        assert_equal(plan["witness_active"], True)
        assert_equal(plan["active_weight_cap"], 1600000)
        assert_equal(plan["transactions"], [])
        assert_equal(set(node.getrawmempool()), before)
        assert_greater_than(plan["legacy_utxo_count"], 17)
        assert_greater_than(plan["inputs_per_full_batch"], 16)
        assert_greater_than(plan["estimated_tx_count"], 0)
        self.log.info("RPC MEASURE legacy_utxo_count=%s inputs_per_full_batch=%s estimated_tx_count=%s estimated_full_batch_weight=%s next=%s" % (
            plan["legacy_utxo_count"], plan["inputs_per_full_batch"], plan["estimated_tx_count"],
            plan["estimated_full_batch_weight"], plan["next_height"]))

        other = node.getnewaddress("", "dilithium-legacy")
        assert_equal(node.pqmigratelegacy(other, native, 1, 1600000, "normal", True, False)["legacy_utxo_count"], 0)
        try:
            node.pqmigratelegacy(other, native, 1, 1600000, "normal", False, False)
            raise AssertionError("empty source was signed")
        except Exception as exc:
            assert "No confirmed spendable" in str(exc)

        signed = node.pqmigratelegacy(legacy, native, 10, 1600000, "normal", False, False)
        assert_equal(signed["batches_built"], 1)
        assert_equal(signed["unbroadcast_limit"].startswith("One signed"), True)
        tx = signed["transactions"][0]
        assert_equal(tx["broadcast"], False)
        assert_greater_than(tx["inputs_used"], 16)
        assert_greater_than(tx["weight"], 400000)
        assert tx["weight"] <= 1600000
        assert_greater_than(tx["fee"], 0)
        assert tx["txid"] not in node.getrawmempool()
        assert_equal(node.sendrawtransaction(tx["hex"]), tx["txid"])
        assert tx["txid"] in node.getrawmempool()
        self.log.info("RPC SIGNED inputs_used=%s weight=%s vsize=%s fee=%s" % (
            tx["inputs_used"], tx["weight"], tx["vsize"], tx["fee"]))
        self.restart_node(0, extra_args=[
            "-conf=bitcoin.conf",
            "-persistmempool=1",
            "-mocktime=%d" % int(self.mock_time),
        ])
        node = self.nodes[0]
        assert tx["txid"] in node.getrawmempool()

        again = node.pqmigratelegacy(legacy, native, 1, 1600000, "normal", False, False)
        used = {(i["txid"], i["vout"]) for i in tx["inputs"]}
        if again["transactions"]:
            reused = {(i["txid"], i["vout"]) for i in again["transactions"][0]["inputs"]}
            assert used.isdisjoint(reused)
            node.sendrawtransaction(again["transactions"][0]["hex"])

        self.mine(1, legacy)
        assert tx["txid"] not in node.getrawmempool()
        info = node.getaddressinfo(signed["destination_address"])
        assert_equal(info["ismine"], True)
        # The native output is spendable through the existing PQ send RPC.
        node.pqsendtoaddress(signed["destination_address"], native, Decimal("1"), "normal")

        try:
            node.pqmigratelegacy("all", legacy, 1, 1600000, "normal", True, False)
            raise AssertionError("legacy destination was accepted")
        except Exception as exc:
            assert "native PQ witness" in str(exc)

        try:
            node.pqmigratelegacy("all", native, 1, 50000, "normal", True, False)
            raise AssertionError("tiny weight cap was accepted")
        except Exception as exc:
            assert "max_tx_weight" in str(exc)

        broad = node.pqmigratelegacy("all", "", 2, 400000, "normal", False, True)
        assert_greater_than(broad["batches_built"], 0)
        assert broad["batches_built"] <= 2
        assert broad["estimated_tx_count"] >= broad["batches_built"]
        for item in broad["transactions"]:
            assert_equal(item["broadcast"], True)
            assert item["weight"] <= 400000
            assert item["txid"] in node.getrawmempool()

        self.mine(2, legacy)
        later = node.pqmigratelegacy(legacy, native, 1, 1600000, "normal", True, False)
        assert_equal(later["witness_active"], True)
        assert_equal(later["active_weight_cap"], 1600000)
        assert_greater_than(later["next_height"], 130)
        assert "policy_end_height" not in later
        assert "window_open" not in later


if __name__ == "__main__":
    PqMigrateLegacyTest(__file__).main()
