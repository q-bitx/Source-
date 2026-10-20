#!/usr/bin/env python3
# Copyright (c) 2026 The Q-BitX Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""RPC crash-safety and standard Dilithium sendtoaddress coverage."""

import os
import time
from decimal import Decimal

from test_framework.authproxy import JSONRPCException
from test_framework import messages
from test_framework.messages import CTransaction, msg_block, msg_tx
from test_framework.p2p import P2PInterface
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than, assert_raises_rpc_error


MAX_STANDARD_TX_WEIGHT = 400000
MIGRATION_CAP = 1600000


class PqRpcSafetyTest(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-conf=bitcoin.conf", "-deprecatedrpc=settxfee"]]
        self.rpc_timeout = 180

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def mine(self, count, address):
        node = self.nodes[0]
        for _ in range(count):
            self.mock_time += 150
            node.setmocktime(self.mock_time)
            self.generatetoaddress(node, 1, address)

    def expect_rpc_error(self, code, text):
        def _call(fn, *args):
            assert_raises_rpc_error(code, text, fn, *args)
            self.nodes[0].getblockcount()
        return _call

    def run_test(self):
        node = self.nodes[0]
        self.mock_time = int(time.time()) - 200000
        legacy = node.getnewaddress("", "dilithium-legacy")
        self.log.info("Negative RPC inputs must return errors and leave the node up")
        err = self.expect_rpc_error
        err(-32601, "Method not found")(node.not_a_qbitx_command)
        err(-1, None)(node.sendtoaddress)
        err(-3, None)(node.sendtoaddress, 1, 1)
        err(-5, None)(node.sendtoaddress, "not-an-address", 1)
        err(-8, "Dilithium message signing is not supported")(node.signmessage, legacy, "hello")
        err(-5, "Invalid private key")(node.signmessagewithprivkey, "not-a-key", "hello")
        err(-22, None)(node.decoderawtransaction, "zz")
        err(-22, None)(node.sendrawtransaction, "00")
        err(-22, None)(node.submitblock, "00")
        err(-22, None)(node.decodepsbt, "not-a-psbt")
        err(-8, None)(node.estimatesmartfee, -1)
        messages.MAGIC_BYTES["regtest"] = b"rgts"
        self.mine(11, legacy)
        native = node.getnewaddress("", "dilithium-bech32")
        assert native.startswith("rdil1")
        err(-8, "Dilithium message signing is not supported")(node.signmessage, native, "hello")
        peer = node.add_p2p_connection(P2PInterface())
        peer.send_without_ping(msg_tx(CTransaction()))
        peer.send_without_ping(msg_block())
        time.sleep(0.5)
        node.getblockcount()
        self.mine(90, legacy)
        node.settxfee(Decimal("0.00005000"))
        start_balance = node.getbalance()
        assert_greater_than(start_balance, Decimal("10"))
        err(-6, "Insufficient funds")(node.sendtoaddress, legacy, Decimal("1000000"))

        self.log.info("sendtoaddress spends legacy and native Dilithium outputs")
        other = node.getnewaddress("", "dilithium-legacy")
        tx_mm = node.sendtoaddress(other, Decimal("1"))
        self.check_spend(tx_mm, min_weight=15000, max_weight=100000, check_fee=True)
        tx_mn = node.sendtoaddress(native, Decimal("0.5"))
        self.check_spend(tx_mn, min_weight=15000, max_weight=100000, check_fee=True)
        pq = node.pqsendfrom(legacy, other, Decimal("0.3"), "normal")
        assert "txid" in pq
        raw_pq = node.getrawtransaction(pq["txid"], True)
        assert_greater_than(MAX_STANDARD_TX_WEIGHT, raw_pq["weight"])
        assert_greater_than(MIGRATION_CAP, raw_pq["weight"])
        self.mine(1, legacy)
        native_utxos = [u for u in node.listunspent() if u["address"] == native and u["spendable"]]
        assert native_utxos
        keep = {(u["txid"], u["vout"]) for u in native_utxos}
        lock = [{"txid": u["txid"], "vout": u["vout"]} for u in node.listunspent() if (u["txid"], u["vout"]) not in keep]
        if lock:
            node.lockunspent(False, lock)
        tx_nm = node.sendtoaddress(other, Decimal("0.2"))
        self.check_spend(tx_nm, min_weight=1000, max_weight=100000, check_fee=False)
        tx_nn = node.sendtoaddress(native, Decimal("0.1"))
        self.check_spend(tx_nn, min_weight=1000, max_weight=100000, check_fee=False)
        if lock:
            node.lockunspent(True, lock)

        unconfirmed = node.sendtoaddress(other, Decimal("0.05"))
        err(-5, "not eligible for abandonment")(node.abandontransaction, unconfirmed)
        assert node.gettransaction(unconfirmed)["txid"] == unconfirmed

        backup_dir = os.path.join(self.options.tmpdir, "wallet-backup")
        backed = node.backupwallet(backup_dir)
        assert_equal(backed["method"], "sqlite-backup-api")
        restored = node.restorewallet("restored", backed["backup_path"])
        assert restored["loaded"]
        rw = node.get_wallet_rpc("restored")
        assert_greater_than(rw.getbalance(), Decimal("0"))

        self.restart_node(0)
        self.nodes[0].setmocktime(self.mock_time)
        assert_greater_than(self.nodes[0].getbalance(), Decimal("0"))
        self.nodes[0].sendtoaddress(other, Decimal("0.01"))

    def check_spend(self, txid, min_weight, max_weight, check_fee):
        node = self.nodes[0]
        raw = node.getrawtransaction(txid, True)
        assert_greater_than(raw["weight"], min_weight)
        assert_greater_than(max_weight + 1, raw["weight"])
        assert_greater_than(MAX_STANDARD_TX_WEIGHT, raw["weight"])
        assert_greater_than(MIGRATION_CAP, raw["weight"])
        info = node.gettransaction(txid)
        fee = -info["fee"]
        assert_greater_than(fee, Decimal("0"))
        if check_fee:
            expected = Decimal(raw["vsize"]) * Decimal("0.00000005")
            assert abs(fee - expected) <= Decimal("0.00000010")
        for vin in raw["vin"]:
            script = vin.get("scriptSig", {}).get("hex", "")
            witness = vin.get("txinwitness", [])
            assert script or witness

if __name__ == "__main__":
    PqRpcSafetyTest(__file__).main()
