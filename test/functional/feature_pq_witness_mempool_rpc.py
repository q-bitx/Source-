#!/usr/bin/env python3
# Copyright (c) 2026 The Q-BitX Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Mempool RPC virtual size matches getblock tx fields after PQ witness activation (regtest).

Covers legacy + bech32 funding, listunspent, testmempoolaccept, getmempoolentry, getblock verbosity 2.
"""

from decimal import Decimal

from test_framework.blocktools import COINBASE_MATURITY
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than


class PQWitnessMempoolRpcTest(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Mature coinbase (%d blocks)", COINBASE_MATURITY + 1)
        node.generate(COINBASE_MATURITY + 1)
        assert_equal(node.getblockchaininfo()["blocks"], COINBASE_MATURITY + 1)

        self.log.info("Send to legacy and bech32; mine one block")
        addr_legacy = node.getnewaddress("", "legacy")
        addr_witness = node.getnewaddress("", "bech32")
        node.sendtoaddress(addr_legacy, Decimal("25"))
        node.sendtoaddress(addr_witness, Decimal("25"))
        node.generate(1)
        tip = node.getblockchaininfo()["blocks"]
        assert_equal(tip, COINBASE_MATURITY + 2)

        self.log.info("Mine to height 119 (next block uses k=16; default regtest PQ witness activates at height 10)")
        need = 119 - tip
        assert_greater_than(need, 0)
        node.generate(need)
        assert_equal(node.getblockchaininfo()["blocks"], 119)

        unspent = node.listunspent(minconf=1)
        assert_greater_than(len(unspent), 0)
        spend = None
        for u in unspent:
            if u["spendable"] and u["amount"] >= Decimal("5") and u.get("address") == addr_witness:
                spend = u
                break
        assert spend is not None

        addr_out = node.getnewaddress("", "bech32")
        raw = node.createrawtransaction(
            [{"txid": spend["txid"], "vout": spend["vout"]}],
            {addr_out: Decimal("4")},
        )
        signed = node.signrawtransactionwithwallet(raw)
        assert signed["complete"]
        hex_tx = signed["hex"]

        self.log.info("testmempoolaccept, sendrawtransaction, getmempoolentry agree on vsize")
        tma = node.testmempoolaccept([hex_tx])[0]
        assert_equal(tma["allowed"], True)
        vsize = tma["vsize"]

        txid = node.sendrawtransaction(hex_tx, 0)
        entry = node.getmempoolentry(txid)
        assert_equal(entry["vsize"], vsize)

        node.generate(1)
        assert_equal(node.getblockchaininfo()["blocks"], 120)

        block = node.getblock(node.getbestblockhash(), 2)
        found = False
        for tx in block["tx"]:
            if tx.get("txid") == txid:
                assert_equal(tx["vsize"], vsize)
                assert_equal(tx["witness_discount_scale"], 16)
                found = True
                break
        assert found

        still = [u for u in node.listunspent(minconf=1) if u["txid"] == spend["txid"] and u["vout"] == spend["vout"]]
        assert_equal(still, [])


if __name__ == "__main__":
    PQWitnessMempoolRpcTest(__file__).main()
