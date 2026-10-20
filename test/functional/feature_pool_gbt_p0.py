#!/usr/bin/env python3
# Copyright (c) 2026 The Q-BitX Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regtest getblocktemplate plus a simulated external pool coinbase.

This is a simulation of an external coinbase. It does not load a SoloPool config.
"""

import time
from decimal import Decimal

from test_framework.blocktools import (
    COINBASE_MATURITY,
    NORMAL_GBT_REQUEST_PARAMS,
    add_witness_commitment,
    create_block,
    create_coinbase,
)
from test_framework.messages import (
    CTxOut,
    tx_from_hex,
    uint256_from_compact,
)
from test_framework.script import CScript, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than

MAX_BLOCK_WEIGHT = 8000000
MAX_BLOCK_SERIALIZED = 8000000
MAX_COINBASE_SCRIPTSIG = 100
POOL_EXTRA_OUTPUT_BYTES = 2048
PQ_SCALE = 16


def sats(amount):
    return int(Decimal(amount) * Decimal(100000000))


def solve_bounded(block, limit=1_000_000):
    block.rehash()
    target = uint256_from_compact(block.nBits)
    attempts = 0
    while block.sha256 > target:
        assert attempts < limit, "regtest proof-of-work search exceeded %d attempts (nBits=%08x)" % (limit, block.nBits)
        block.nNonce += 1
        block.rehash()
        attempts += 1


class PoolGbtP0Test(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        # Q-BitX reads qbitx.conf. The framework writes bitcoin.conf, which
        # selects regtest, the isolated ports, and -connect=0.
        self.extra_args = [["-conf=bitcoin.conf"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def script_for(self, address):
        raw = self.nodes[0].createrawtransaction([], {address: 1})
        return bytes.fromhex(self.nodes[0].decoderawtransaction(raw)["vout"][0]["scriptPubKey"]["hex"])

    def mine(self, count):
        node = self.nodes[0]
        for _ in range(count):
            self.mock_time += 150
            node.setmocktime(self.mock_time)
            self.generatetoaddress(node, 1, self.legacy)

    def pool_coinbase(self, tmpl, extra_scripts):
        payout = CScript(self.script_for(self.legacy))
        coinbase = create_coinbase(height=tmpl["height"], script_pubkey=payout)
        coinbase.vout[0].nValue = tmpl["coinbasevalue"]
        raw_sig = bytes(coinbase.vin[0].scriptSig)
        assert len(raw_sig) <= MAX_COINBASE_SCRIPTSIG
        coinbase.vin[0].scriptSig = CScript(raw_sig + bytes(MAX_COINBASE_SCRIPTSIG - len(raw_sig)))
        for script in extra_scripts:
            coinbase.vout.append(CTxOut(0, script))
        coinbase.rehash()
        return coinbase

    def assemble(self, tmpl, coinbase):
        txs = [tx_from_hex(entry["data"]) for entry in tmpl["transactions"]]
        block = create_block(tmpl=tmpl, coinbase=coinbase, txlist=txs)
        add_witness_commitment(block)
        return block

    def run_test(self):
        node = self.nodes[0]
        self.mock_time = int(time.time())
        self.legacy = node.getnewaddress("", "dilithium-legacy")
        # Regtest: block-limit height 5, PQ witness height 10. Space blocks by the
        # 150s LWMA target so generatetoaddress stays at minimum difficulty.
        # Native PQ addresses are refused until that height.
        self.mine(COINBASE_MATURITY + 1)
        assert_greater_than(node.getblockchaininfo()["blocks"], 10)
        native = node.getnewaddress("", "dilithium-bech32")
        assert native.startswith("rdil1")

        legacy_dest = node.getnewaddress("", "dilithium-legacy")
        native_dest = node.getnewaddress("", "dilithium-bech32")
        node.pqsendtoaddress(self.legacy, legacy_dest, Decimal("2"), "normal")
        node.pqsendtoaddress(self.legacy, native_dest, Decimal("2"), "normal")
        self.mine(1)
        legacy_send = node.pqsendtoaddress(legacy_dest, node.getnewaddress("", "dilithium-legacy"), Decimal("1"), "normal")
        native_send = node.pqsendtoaddress(native_dest, node.getnewaddress("", "dilithium-bech32"), Decimal("1"), "normal")
        assert_equal(legacy_send["tx_count"], 1)
        assert_equal(native_send["tx_count"], 1)
        assert_equal(legacy_send["details"][0]["debug_native_pq_witness"], False)
        assert_equal(native_send["details"][0]["debug_native_pq_witness"], True)
        legacy_txid = legacy_send["txids"][0]
        native_txid = native_send["txids"][0]
        assert legacy_txid in node.getrawmempool()
        assert native_txid in node.getrawmempool()

        first = node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)
        second = node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)
        gbt_ids = [tx["txid"] for tx in first["transactions"]]
        assert legacy_txid in gbt_ids
        assert native_txid in gbt_ids
        assert_equal([tx["txid"] for tx in second["transactions"]], gbt_ids)
        assert_equal(first["height"], node.getblockchaininfo()["blocks"] + 1)

        mempool_by_txid = {}
        for entry in first["transactions"]:
            assert "data" in entry and entry["data"]
            assert "txid" in entry and "weight" in entry and "fee" in entry
            decoded = tx_from_hex(entry["data"])
            decoded.rehash()
            assert_equal(decoded.hash, entry["txid"])
            mem = node.getmempoolentry(entry["txid"])
            assert_equal(mem["weight"], entry["weight"])
            assert_equal(sats(mem["fees"]["base"]), entry["fee"])
            assert_equal(mem["vsize"], (entry["weight"] + PQ_SCALE - 1) // PQ_SCALE)
            raw = bytes.fromhex(node.gettransaction(entry["txid"])["hex"])
            assert_greater_than(len(raw), 0)
            if entry["txid"] == native_txid:
                assert_greater_than(len(raw) * PQ_SCALE, entry["weight"])
                assert_greater_than(len(raw), mem["vsize"])
            mempool_by_txid[entry["txid"]] = {"weight": entry["weight"], "fee": entry["fee"], "vsize": mem["vsize"]}

        payload = b"\x11" * 1800
        extra = CScript([payload])
        extra_bytes = len(CTxOut(0, extra).serialize())
        assert extra_bytes <= POOL_EXTRA_OUTPUT_BYTES
        coinbase = self.pool_coinbase(first, [extra])
        assert_equal(len(coinbase.vin[0].scriptSig), MAX_COINBASE_SCRIPTSIG)
        block = self.assemble(first, coinbase)
        serialized = len(block.serialize())
        assert serialized <= MAX_BLOCK_SERIALIZED
        solve_bounded(block)
        tip_before = node.getbestblockhash()
        assert_equal(node.submitblock(block.serialize().hex()), None)
        assert_equal(node.getbestblockhash(), block.hash)
        assert node.getbestblockhash() != tip_before

        confirmed = node.getblock(block.hash, 2)
        assert_equal(confirmed["weight"] <= MAX_BLOCK_WEIGHT, True)
        confirmed_ids = [tx["txid"] for tx in confirmed["tx"]]
        for txid, snap in mempool_by_txid.items():
            assert txid in confirmed_ids
            assert txid not in node.getrawmempool()
            found = next(tx for tx in confirmed["tx"] if tx["txid"] == txid)
            assert_equal(found["weight"], snap["weight"])
            assert_equal(found["vsize"], snap["vsize"])
            assert_equal(sats(found["fee"]), snap["fee"])
        assert_equal(node.getmempoolinfo()["size"], 0)

        # Negative simulation: a later template with a coinbase large enough to
        # exceed MAX_BLOCK_WEIGHT. submitblock must reject it and leave the
        # mempool transaction unconfirmed. This is not a live SoloPool config.
        self.mine(1)
        again_dest = node.getnewaddress("", "dilithium-legacy")
        again = node.pqsendtoaddress(self.legacy, again_dest, Decimal("1"), "normal")
        again_txid = again["txids"][0]
        tmpl = node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS)
        assert again_txid in [tx["txid"] for tx in tmpl["transactions"]]
        over = self.assemble(tmpl, self.pool_coinbase(tmpl, []))
        filler = CScript(bytes([OP_TRUE]) * 10000)
        per_output = len(CTxOut(0, filler).serialize())
        # Non-witness bytes weigh PQ_SCALE. Cross the consensus weight cap.
        need_bytes = (MAX_BLOCK_WEIGHT // PQ_SCALE) + per_output
        n_out = (need_bytes + per_output - 1) // per_output
        for _ in range(n_out):
            over.vtx[0].vout.append(CTxOut(0, filler))
        over.vtx[0].rehash()
        over.hashMerkleRoot = over.calc_merkle_root()
        over.rehash()
        assert len(over.serialize()) > 0
        solve_bounded(over)
        tip = node.getbestblockhash()
        rejected = node.submitblock(over.serialize().hex())
        assert rejected is not None and rejected != ""
        assert_equal(node.getbestblockhash(), tip)
        assert again_txid in node.getrawmempool()


if __name__ == "__main__":
    PoolGbtP0Test(__file__).main()
