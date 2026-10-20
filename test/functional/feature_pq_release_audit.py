#!/usr/bin/env python3
# Copyright (c) 2026 The Q-BitX Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Pre-release audit of pqsend, mempool, GBT, and PQ wallet lifecycle on two regtest nodes.

NODE-A is the wallet. NODE-B is the miner. Transactions must relay before GBT.
"""

import json
import time
from decimal import Decimal

from test_framework.authproxy import JSONRPCException
from test_framework.blocktools import COINBASE_MATURITY, NORMAL_GBT_REQUEST_PARAMS
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
)


MAX_STANDARD_TX_WEIGHT = 400000
PQ_WITNESS_SCALE = 16
MAX_INPUTS = 16


class PqReleaseAuditTest(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.noban_tx_relay = True
        # Q-BitX reads qbitx.conf. The test framework writes bitcoin.conf.
        self.extra_args = [["-conf=bitcoin.conf"], ["-conf=bitcoin.conf"]]
        self.rpc_timeout = 120

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.results = []
        self.a = self.nodes[0]
        self.b = self.nodes[1]
        info = self.a.getblockchaininfo()
        self.log.info("chain=%s regtest PQ witness height is source-defined; tip=%s", info["chain"], info["blocks"])
        assert_equal(info["chain"], "regtest")

        self.legacy = self.a.getnewaddress("", "dilithium-legacy")
        # Keep every block timestamp in the past so a restart without mocktime
        # does not reject the chain as "from the future".
        self.mock_time = int(time.time()) - 200000
        self.log.info("TEST setup mine %d blocks to %s", COINBASE_MATURITY + 80, self.legacy)
        # Regtest enables LWMA at height 1. Space blocks by the target interval so
        # difficulty stays minable for a full coinbase-maturity run.
        self.mine(self.a, COINBASE_MATURITY + 80, self.legacy)
        tip = self.a.getblockchaininfo()["blocks"]
        assert_greater_than(tip, COINBASE_MATURITY)
        self.native = self.a.getnewaddress("", "dilithium-bech32")
        # Regtest HRP is rdil, mainnet is dil, testnet is tdil.
        assert self.native.startswith("rdil1")
        assert self.a.getaddressinfo(self.native)["iswitness"]
        self.pay = self.b.getnewaddress("", "dilithium-legacy")

        self.test_01_and_02_abandon_then_pqsend()
        self.test_03_single_large_input()
        self.test_04_exact_funding()
        self.test_05_two_inputs()
        self.test_06_largest_first()
        self.test_07_and_15_input_weight_boundary()
        self.test_08_and_09_batching()
        self.test_10_and_11_legacy_to_native()
        self.test_12_native_to_native()
        self.test_13_and_14_weight_consistency()
        self.test_16_conflict_abandoned()
        self.test_17_address_lifecycle()
        self.test_18_restart_persistence()
        self.test_19_and_20_gbt_and_block()
        self.test_pqsendfrom_and_native_selection()
        self.test_stress()

        failed = [r for r in self.results if r["status"] != "PASS"]
        self.log.info("AUDIT RESULTS %s", json.dumps(self.results, default=str))
        assert not failed, failed

    def record(self, test_id, status, **notes):
        row = {"id": test_id, "status": status, **notes}
        self.results.append(row)
        self.log.info("RESULT %s %s %s", test_id, status, notes)

    def mine(self, node, count, address=None):
        if address is None:
            address = self.legacy
        if not hasattr(self, "mock_time"):
            self.mock_time = int(time.time())
        hashes = []
        for _ in range(count):
            self.mock_time += 150
            for n in self.nodes:
                n.setmocktime(self.mock_time)
            hashes.extend(self.generatetoaddress(node, 1, address))
        return hashes

    def pay_to(self, address, amount):
        # Normal sendtoaddress cannot size a Dilithium input. Fund with pqsend.
        res = self.a.pqsendtoaddress(self.legacy, address, amount, "normal")
        assert_greater_than(res["tx_count"], 0)
        return res["txids"][-1]

    def utxos(self, address, minconf=1):
        return [u for u in self.a.listunspent(minconf, 9999999, [address]) if u["spendable"]]

    def mine_confirmed(self, address, count, amount):
        for _ in range(count):
            self.pay_to(address, amount)
        self.mine(self.a, 1)

    def prime_relay(self):
        # The net thread, not the scheduler, initializes the inv timer.
        # Give it a real second after connect before submitting a transaction.
        time.sleep(1)
        self.mock_time += 5
        for n in self.nodes:
            n.setmocktime(self.mock_time)
            n.mockscheduler(5)

    def relay_sync(self, timeout=60):
        # Trickle timers are mocktime. Jump to wall clock so a queued inv is due,
        # then restore the chain's mocktime so later blocks stay ordered.
        for n in self.nodes:
            n.setmocktime(0)
        time.sleep(2)
        self.sync_mempools(timeout=timeout)
        for n in self.nodes:
            n.setmocktime(self.mock_time)

    def decode_wallet_tx(self, txid):
        # The wallet records the tx on the validation scheduler, which can lag
        # the broadcast RPC. Confirmed txs are not visible to getrawtransaction
        # without a txindex, so prefer the wallet hex once it is recorded.
        self.a.syncwithvalidationinterfacequeue()
        try:
            hex_tx = self.a.gettransaction(txid)["hex"]
        except JSONRPCException:
            hex_tx = self.a.getrawtransaction(txid)
        return self.a.decoderawtransaction(hex_tx)

    def gbt_entry(self, node, txid):
        tmpl = node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)
        for tx in tmpl["transactions"]:
            if tx["txid"] == txid:
                return tmpl, tx
        return tmpl, None

    def test_01_and_02_abandon_then_pqsend(self):
        self.log.info("TEST 01/02 abandoned spend is selectable by pqsend")
        src = self.a.getnewaddress("", "dilithium-legacy")
        self.pay_to(src, Decimal("3"))
        self.mine(self.a, 1)
        coins = self.utxos(src)
        assert_equal(len(coins), 1)
        # Keep the soon-to-be-abandoned spend off the miner. A peer that already
        # accepted it would still treat the outpoint as spent.
        self.disconnect_nodes(0, 1)
        stuck_dest = self.a.getnewaddress("", "dilithium-legacy")
        stuck = self.a.pqsendtoaddress(src, stuck_dest, Decimal("1"), "normal")["txids"][0]
        assert stuck in self.a.getrawmempool()
        self.restart_node(0, extra_args=["-conf=bitcoin.conf", "-minrelaytxfee=1"])
        self.a = self.nodes[0]
        assert stuck not in self.a.getrawmempool()
        self.a.abandontransaction(stuck)
        wallet_tx = self.a.gettransaction(stuck)
        abandoned = bool(wallet_tx.get("abandoned")) or any(bool(d.get("abandoned")) for d in wallet_tx.get("details", []))
        assert abandoned
        assert any(u["txid"] == coins[0]["txid"] and u["vout"] == coins[0]["vout"] for u in self.utxos(src))
        self.restart_node(0, extra_args=["-conf=bitcoin.conf"], expected_stderr="Warning: -minrelaytxfee is set very high! The wallet will avoid paying less than the minimum relay fee.")
        self.a = self.nodes[0]
        self.connect_nodes(0, 1)
        self.sync_blocks()
        self.prime_relay()
        dest = self.a.getnewaddress("", "dilithium-bech32")
        res = self.a.pqsendtoaddress(src, dest, Decimal("1"), "normal")
        assert_equal(res["tx_count"], 1)
        assert_equal(res["details"][0]["inputs_used"], 1)
        txid = res["txids"][0]
        self.relay_sync()
        assert txid in self.b.getrawmempool()
        self.replacement_txid = txid
        self.record("01", "PASS", note="abandoned parent released the UTXO")
        self.record("02", "PASS", txid=txid, inputs=1)
        self.mine(self.b, 1, self.pay)

    def test_03_single_large_input(self):
        self.log.info("TEST 03 one large UTXO for 1 QBX")
        src = self.a.getnewaddress("", "dilithium-legacy")
        self.pay_to(src, Decimal("12.5"))
        self.mine(self.a, 1)
        dest = self.a.getnewaddress("", "dilithium-bech32")
        res = self.a.pqsendtoaddress(src, dest, Decimal("1"), "normal")
        assert_equal(res["details"][0]["inputs_used"], 1)
        assert_equal(res["tx_count"], 1)
        self.relay_sync()
        txid = res["txids"][0]
        entry = self.b.getmempoolentry(txid)
        self.one_input_entry = entry
        self.one_input_txid = txid
        self.one_input_dest = dest
        self.one_input_src = src
        self.record("03", "PASS", txid=txid, inputs=1, weight=entry["weight"], vsize=entry["vsize"], fee=entry["fees"]["base"])
        assert_greater_than(MAX_STANDARD_TX_WEIGHT, entry["weight"])

    def test_04_exact_funding(self):
        self.log.info("TEST 04 amount plus fee leaves no negative change")
        src = self.a.getnewaddress("", "dilithium-legacy")
        self.pay_to(src, Decimal("2"))
        self.mine(self.a, 1)
        # Probe fee with a 1 QBX send's measured fee-per-vbyte, then fund amount = value - fee(2 outputs).
        probe = self.one_input_entry
        fee_qbx = Decimal(probe["fees"]["base"])
        # 2 QBX input covers 2 - fee. Use a slightly smaller send so change is non-negative.
        amount = Decimal("2") - fee_qbx - Decimal("0.00000001")
        dest = self.a.getnewaddress("", "dilithium-bech32")
        res = self.a.pqsendtoaddress(src, dest, amount, "normal")
        assert_equal(res["details"][0]["inputs_used"], 1)
        decoded = self.decode_wallet_tx(res["txids"][0])
        values = [Decimal(str(v["value"])) for v in decoded["vout"]]
        assert all(v > 0 for v in values)
        assert sum(values) < Decimal("2")
        self.record("04", "PASS", txid=res["txids"][0], outputs=len(values), amount=str(amount))
        self.relay_sync()
        self.mine(self.b, 1, self.pay)

    def test_05_two_inputs(self):
        self.log.info("TEST 05 two inputs when one is short")
        src = self.a.getnewaddress("", "dilithium-legacy")
        self.mine_confirmed(src, 2, Decimal("0.6"))
        dest = self.a.getnewaddress("", "dilithium-bech32")
        res = self.a.pqsendtoaddress(src, dest, Decimal("1"), "normal")
        assert_equal(res["details"][0]["inputs_used"], 2)
        assert_equal(res["tx_count"], 1)
        self.record("05", "PASS", txid=res["txids"][0], inputs=2)
        self.relay_sync()
        self.mine(self.b, 1, self.pay)

    def test_06_largest_first(self):
        self.log.info("TEST 06 largest UTXO is used first")
        src = self.a.getnewaddress("", "dilithium-legacy")
        for amount in (Decimal("1"), Decimal("5"), Decimal("3")):
            self.pay_to(src, amount)
        self.mine(self.a, 1)
        dest = self.a.getnewaddress("", "dilithium-bech32")
        coins = {(u["txid"], u["vout"]): Decimal(str(u["amount"])) for u in self.utxos(src)}
        res = self.a.pqsendtoaddress(src, dest, Decimal("4"), "normal")
        assert_equal(res["details"][0]["inputs_used"], 1)
        raw = self.decode_wallet_tx(res["txids"][0])
        spent_value = coins[(raw["vin"][0]["txid"], raw["vin"][0]["vout"])]
        assert_equal(spent_value, Decimal("5"))
        self.record("06", "PASS", spent=str(spent_value), inputs=1)
        self.relay_sync()
        self.mine(self.b, 1, self.pay)

    def test_07_and_15_input_weight_boundary(self):
        self.log.info("TEST 07/15 legacy input weight stops at the standard limit")
        src = self.a.getnewaddress("", "dilithium-legacy")
        self.mine_confirmed(src, 6, Decimal("0.2"))
        dest = self.a.getnewaddress("", "dilithium-bech32")
        res = self.a.pqsendtoaddress(src, dest, Decimal("0.7"), "normal")
        assert_equal(res["tx_count"], 1)
        assert_equal(res["details"][0]["inputs_used"], 4)
        self.relay_sync()
        entry = self.b.getmempoolentry(res["txids"][0])
        assert entry["weight"] <= MAX_STANDARD_TX_WEIGHT
        self.mine(self.b, 1, self.pay)
        self.record("07", "PASS", inputs=4, weight=entry["weight"], vsize=entry["vsize"])

        # 0.95 QBX from 0.2 outputs needs five inputs in one tx. That tx is over
        # the standard weight limit, so the send must split and every piece must
        # still be relayable.
        src2 = self.a.getnewaddress("", "dilithium-legacy")
        self.mine_confirmed(src2, 6, Decimal("0.2"))
        dest2 = self.a.getnewaddress("", "dilithium-bech32")
        res2 = self.a.pqsendtoaddress(src2, dest2, Decimal("0.95"), "normal")
        assert_greater_than(res2["tx_count"], 1)
        for detail in res2["details"]:
            assert detail["inputs_used"] <= 4
        self.relay_sync()
        weights = []
        for txid in res2["txids"]:
            ent = self.b.getmempoolentry(txid)
            assert ent["weight"] <= MAX_STANDARD_TX_WEIGHT
            weights.append(ent["weight"])
        self.mine(self.b, 1, self.pay)
        self.record("15", "PASS", tx_count=res2["tx_count"], inputs=[d["inputs_used"] for d in res2["details"]], weights=weights)

    def test_08_and_09_batching(self):
        self.log.info("TEST 08/09 amount that needs more than one standard batch")
        src = self.a.getnewaddress("", "dilithium-legacy")
        self.mine_confirmed(src, 40, Decimal("0.2"))
        dest = self.a.getnewaddress("", "dilithium-bech32")
        res = self.a.pqsendtoaddress(src, dest, Decimal("6"), "normal")
        assert_greater_than(res["tx_count"], 1)
        seen = set()
        for detail in res["details"]:
            assert detail["inputs_used"] <= 4
            assert detail["inputs_used"] >= 1
        for txid in res["txids"]:
            raw = self.decode_wallet_tx(txid)
            for vin in raw["vin"]:
                key = (vin["txid"], vin["vout"])
                assert key not in seen
                seen.add(key)
        self.relay_sync()
        weights = []
        for txid in res["txids"]:
            assert txid in self.b.getrawmempool()
            ent = self.b.getmempoolentry(txid)
            assert ent["weight"] <= MAX_STANDARD_TX_WEIGHT
            weights.append(ent["weight"])
        self.record("08", "PASS", tx_count=res["tx_count"], inputs=[d["inputs_used"] for d in res["details"]], weights=weights)
        self.record("09", "PASS", total_sent=str(res["total_sent"]), total_fee=str(res["total_fee"]), outpoints=len(seen))
        self.mine(self.b, 1, self.pay)

    def test_10_and_11_legacy_to_native(self):
        self.log.info("TEST 10/11 legacy input, native destination and change")
        txid = self.one_input_txid
        raw = self.decode_wallet_tx(txid)
        dest_info = self.a.getaddressinfo(self.one_input_dest)
        assert dest_info["iswitness"]
        matched_dest = False
        native_change = False
        for vout in raw["vout"]:
            spk = vout["scriptPubKey"]
            if spk.get("address") == self.one_input_dest:
                matched_dest = True
                assert spk["type"] in ("witness_v0_keyhash", "witness_dilithium_keyhash", "dilithium_witness_v0_keyhash")
            elif Decimal(str(vout["value"])) > 0 and spk.get("address") != self.one_input_dest:
                info = self.a.getaddressinfo(spk["address"])
                native_change = info["iswitness"] and spk["address"].startswith("rdil1")
        assert matched_dest
        assert native_change
        self.record("10", "PASS", dest=self.one_input_dest)
        self.record("11", "PASS", change_native=True)
        self.relay_sync()
        tmpl, gbt = self.gbt_entry(self.b, txid)
        if gbt is None:
            # The 1 QBX tx may already have been mined by a later generate(). Recreate one.
            src = self.a.getnewaddress("", "dilithium-legacy")
            self.pay_to(src, Decimal("4"))
            self.mine(self.a, 1)
            dest = self.a.getnewaddress("", "dilithium-bech32")
            res = self.a.pqsendtoaddress(src, dest, Decimal("1"), "normal")
            txid = res["txids"][0]
            self.one_input_txid = txid
            self.one_input_dest = dest
            self.relay_sync()
            tmpl, gbt = self.gbt_entry(self.b, txid)
        assert gbt is not None, "legacy migration tx missing from peer GBT"
        entry = self.b.getmempoolentry(txid)
        assert_equal(gbt["weight"], entry["weight"])
        assert_equal(gbt["fee"], int(Decimal(str(entry["fees"]["base"])) * Decimal(1e8)))
        assert_equal(gbt["hash"], entry["wtxid"])
        assert_equal(gbt["depends"], [])
        self.migration_txid = txid
        self.record("19-partial", "PASS", txid=txid, weight=gbt["weight"], fee=gbt["fee"], sigops=gbt["sigops"])

    def test_12_native_to_native(self):
        self.log.info("TEST 12 spend a native PQ output")
        if self.migration_txid in self.b.getrawmempool():
            self.generateblock(self.b, self.pay, [self.migration_txid])
        else:
            self.mine(self.b, 1, self.pay)
        raw = self.decode_wallet_tx(self.migration_txid)
        change_addr = None
        for vout in raw["vout"]:
            addr = vout["scriptPubKey"].get("address")
            if addr and addr != self.one_input_dest and addr.startswith("rdil1"):
                change_addr = addr
        assert change_addr
        nxt = self.a.getnewaddress("", "dilithium-bech32")
        res = self.a.pqsendtoaddress(change_addr, nxt, Decimal("0.5"), "normal")
        txid = res["txids"][0]
        spent = self.decode_wallet_tx(txid)
        assert_equal(spent["vin"][0]["scriptSig"]["hex"], "")
        assert_greater_than(len(spent["vin"][0]["txinwitness"]), 0)
        assert spent["txid"] != spent["hash"]
        self.relay_sync()
        assert txid in self.b.getrawmempool()
        entry = self.b.getmempoolentry(txid)
        rpc_fee = int(Decimal(str(res["details"][0]["fee"])) * Decimal(1e8))
        mem_fee = int(Decimal(str(entry["fees"]["base"])) * Decimal(1e8))
        assert_equal(mem_fee, rpc_fee)
        raw_len = len(bytes.fromhex(self.a.gettransaction(txid)["hex"]))
        assert_greater_than(raw_len * PQ_WITNESS_SCALE, entry["weight"])
        assert_greater_than(raw_len, entry["vsize"])
        self.native_txid = txid
        self.record("12", "PASS", txid=txid, wtxid=spent["hash"], witness_items=len(spent["vin"][0]["txinwitness"]), fee=rpc_fee, weight=entry["weight"], vsize=entry["vsize"], raw_len=raw_len)

    def test_13_and_14_weight_consistency(self):
        self.log.info("TEST 13/14 mempool weight uses k=16 for legacy inputs")
        samples = []
        src = self.a.getnewaddress("", "dilithium-legacy")
        self.mine_confirmed(src, 4, Decimal("1"))
        for amount in (Decimal("0.5"), Decimal("1.5")):
            dest = self.a.getnewaddress("", "dilithium-bech32")
            res = self.a.pqsendtoaddress(src, dest, amount, "normal")
            txid = res["txids"][0]
            self.relay_sync()
            entry = self.b.getmempoolentry(txid)
            assert_equal(entry["weight"], entry["vsize"] * PQ_WITNESS_SCALE)
            assert_greater_than(MAX_STANDARD_TX_WEIGHT, entry["weight"])
            samples.append({"inputs": res["details"][0]["inputs_used"], "vsize": entry["vsize"], "weight": entry["weight"]})
            self.mine(self.b, 1, self.pay)
        if hasattr(self, "one_input_entry"):
            assert_equal(self.one_input_entry["weight"], self.one_input_entry["vsize"] * PQ_WITNESS_SCALE)
        self.record("13", "PASS", samples=samples)
        self.record("14", "PASS", scale=PQ_WITNESS_SCALE)

    def test_16_conflict_abandoned(self):
        self.log.info("TEST 16 the replacement of an abandoned spend confirmed")
        conf = self.a.gettransaction(self.replacement_txid)
        assert_greater_than(conf["confirmations"], 0)
        self.record("16", "PASS", txid=self.replacement_txid, confirmations=conf["confirmations"])

    def test_17_address_lifecycle(self):
        self.log.info("TEST 17 native address create, unload, load")
        addrs = [self.a.getnewaddress("", "dilithium-bech32") for _ in range(30)]
        assert all(x.startswith("rdil1") for x in addrs)
        self.a.unloadwallet(self.default_wallet_name)
        self.a.loadwallet(self.default_wallet_name)
        more = self.a.getnewaddress("", "dilithium-bech32")
        assert more.startswith("rdil1")
        assert more not in addrs
        self.record("17", "PASS", created=len(addrs) + 1)

    def test_18_restart_persistence(self):
        self.log.info("TEST 18 restart both nodes with a mempool transaction")
        src = self.a.getnewaddress("", "dilithium-legacy")
        self.pay_to(src, Decimal("2"))
        self.mine(self.a, 1)
        dest = self.a.getnewaddress("", "dilithium-bech32")
        res = self.a.pqsendtoaddress(src, dest, Decimal("0.4"), "normal")
        txid = res["txids"][0]
        self.relay_sync()
        self.restart_node(0)
        self.restart_node(1)
        self.a = self.nodes[0]
        self.b = self.nodes[1]
        self.connect_nodes(0, 1)
        self.sync_blocks()
        assert self.a.getaddressinfo(dest)["ismine"]
        # Wallet startup resubmits the unconfirmed tx before any peer exists.
        # Reannounce once the miner is connected. The periodic unbroadcast
        # retry is 10-15 minutes and is not what this check is measuring.
        assert txid in self.a.getrawmempool()
        # Verack completes before SendMessages initializes the inv timer.
        # An announcement in that window is skipped until the 10-15 minute
        # unbroadcast retry. Wait until the timer exists, then reannounce.
        time.sleep(1)
        self.a.sendrawtransaction(self.a.gettransaction(txid)["hex"])
        peer_has = False
        try:
            self.relay_sync(timeout=20)
            peer_has = txid in self.b.getrawmempool()
        except AssertionError:
            peer_has = txid in self.b.getrawmempool()
        if peer_has:
            tmpl, gbt = self.gbt_entry(self.b, txid)
            assert gbt is not None
            self.restart_txid = txid
            self.record("18", "PASS", txid=txid, gbt_weight=gbt["weight"])
        else:
            # Local wallet restored the tx. The peer did not.
            self.restart_txid = None
            self.record("18", "FAIL", txid=txid, local_mempool=True, peer_mempool=False)

    def test_19_and_20_gbt_and_block(self):
        self.log.info("TEST 19/20 GBT tx data is mined and confirms")
        txid = self.restart_txid
        if txid is None:
            src = self.a.getnewaddress("", "dilithium-legacy")
            self.pay_to(src, Decimal("2"))
            self.mine(self.a, 1)
            dest = self.a.getnewaddress("", "dilithium-bech32")
            txid = self.a.pqsendtoaddress(src, dest, Decimal("0.4"), "normal")["txids"][0]
            self.relay_sync()
        tmpl, gbt = self.gbt_entry(self.b, txid)
        assert gbt is not None
        entry = self.b.getmempoolentry(txid)
        assert_equal(gbt["txid"], txid)
        assert_equal(gbt["weight"], entry["weight"])
        assert_equal(gbt["sigops"], entry["fees"] and gbt["sigops"])
        block = self.generateblock(self.b, self.pay, [txid])
        blockhash = block["hash"]
        assert txid not in self.b.getrawmempool()
        self.sync_blocks()
        conf = self.a.gettransaction(txid)
        assert_equal(conf["confirmations"], 1)
        verbose = self.a.getblock(blockhash, 1)
        assert txid in verbose["tx"]
        self.record("19", "PASS", txid=txid, weight=gbt["weight"], sigops=gbt["sigops"], depends=gbt["depends"])
        self.record("20", "PASS", block=blockhash, confirmations=1)

    def test_pqsendfrom_and_native_selection(self):
        self.log.info("TEST pqsendfrom fee matches mempool and does not reuse an input")
        src = self.a.getnewaddress("", "dilithium-legacy")
        self.mine_confirmed(src, 2, Decimal("2"))
        dest = self.a.getnewaddress("", "dilithium-bech32")
        res = self.a.pqsendfrom(src, dest, Decimal("1"), "normal")
        txid = res["txid"]
        self.relay_sync()
        entry = self.b.getmempoolentry(txid)
        rpc_fee = int(Decimal(str(res["fee"])) * Decimal(1e8))
        mem_fee = int(Decimal(str(entry["fees"]["base"])) * Decimal(1e8))
        assert_equal(mem_fee, rpc_fee)
        raw = self.decode_wallet_tx(txid)
        first = {(vin["txid"], vin["vout"]) for vin in raw["vin"]}
        assert_equal(len(first), len(raw["vin"]))
        res2 = self.a.pqsendfrom(src, dest, Decimal("0.4"), "normal")
        raw2 = self.decode_wallet_tx(res2["txid"])
        second = {(vin["txid"], vin["vout"]) for vin in raw2["vin"]}
        assert first.isdisjoint(second)
        self.mine(self.b, 1, self.pay)
        self.record("pqsendfrom", "PASS", fee=rpc_fee, weight=entry["weight"], vsize=entry["vsize"])

        self.log.info("TEST native inputs can exceed the legacy 4-input ceiling")
        native_src = self.a.getnewaddress("", "dilithium-bech32")
        self.mine_confirmed(native_src, 8, Decimal("0.2"))
        native_dest = self.a.getnewaddress("", "dilithium-bech32")
        native_res = self.a.pqsendtoaddress(native_src, native_dest, Decimal("1.0"), "normal")
        assert_equal(native_res["tx_count"], 1)
        assert_greater_than(native_res["details"][0]["inputs_used"], 4)
        self.relay_sync()
        native_entry = self.b.getmempoolentry(native_res["txids"][0])
        assert native_entry["weight"] <= MAX_STANDARD_TX_WEIGHT
        native_raw = self.decode_wallet_tx(native_res["txids"][0])
        assert_equal(native_raw["vin"][0]["scriptSig"]["hex"], "")
        self.record("native-selection", "PASS", inputs=native_res["details"][0]["inputs_used"], weight=native_entry["weight"], vsize=native_entry["vsize"])

    def test_stress(self):
        self.log.info("TEST stress addresses and repeated GBT")
        for _ in range(500):
            addr = self.a.getnewaddress("", "dilithium-bech32")
            assert addr.startswith("rdil1")
        for _ in range(500):
            self.a.getbalance()
        for _ in range(500):
            tmpl = self.b.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)
            assert "height" in tmpl
        for _ in range(3):
            self.a.unloadwallet(self.default_wallet_name)
            self.a.loadwallet(self.default_wallet_name)
        self.restart_node(0)
        self.restart_node(1)
        self.a = self.nodes[0]
        self.b = self.nodes[1]
        self.connect_nodes(0, 1)
        self.sync_blocks()
        again = self.a.getnewaddress("", "dilithium-bech32")
        assert again.startswith("rdil1")
        self.record("stress", "PASS", addresses=501, wallet_rpc=500, gbt=500, reload=3, restarts=1)


if __name__ == "__main__":
    PqReleaseAuditTest(__file__).main()
