#!/usr/bin/env python3
# Copyright (c) 2026 The Q-BitX Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Permanent legacy-to-native migration policy. Not a live SoloPool config."""

import base64
import json
import subprocess
import time
from decimal import Decimal
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from test_framework.blocktools import (
    add_witness_commitment,
    create_block,
    create_coinbase,
)
from test_framework.messages import COIN, CTxOut, tx_from_hex, uint256_from_compact
from test_framework.script import CScript, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than

OLD_QBITX = "/home/sus/pesok/qbitx-v0.3.3-bin/qbitx-v0.3.3-linux/qbitx"
OLD_CLI = "/home/sus/pesok/qbitx-v0.3.3-bin/qbitx-v0.3.3-linux/qbitx-cli"
MAX_BLOCK_WEIGHT = 8000000


def solve_bounded(block, limit=1_000_000):
    block.rehash()
    target = uint256_from_compact(block.nBits)
    attempts = 0
    while block.sha256 > target:
        assert attempts < limit, "proof-of-work search did not finish"
        block.nNonce += 1
        block.rehash()
        attempts += 1


class MigrationPolicyTest(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            "-conf=bitcoin.conf",
            "-persistmempool=1",
        ]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def mine(self, count):
        node = self.nodes[0]
        for _ in range(count):
            self.mock_time += 150
            node.setmocktime(self.mock_time)
            self.generatetoaddress(node, 1, self.legacy)

    def migration_hex(self, utxos, destination, legacy_change=False, fee=Decimal("1")):
        node = self.nodes[0]
        inputs = [{"txid": u["txid"], "vout": u["vout"]} for u in utxos]
        total = sum((u["amount"] for u in utxos), Decimal("0"))
        assert_greater_than(total, fee)
        dest = self.legacy if legacy_change else destination
        raw = node.createrawtransaction(inputs, {dest: total - fee})
        signed = node.signrawtransactionwithwallet(raw)
        assert signed["complete"], signed.get("errors", signed)
        return signed["hex"]

    def start_old(self):
        self.old_dir = Path(self.options.tmpdir) / "old-node"
        self.old_dir.mkdir()
        (self.old_dir / "regtest").mkdir()
        # Isolated conf so the unmodified binary does not read the user's qbitx.conf.
        (self.old_dir / "qbitx.conf").write_text("regtest=1\nserver=1\n")
        self.old = subprocess.Popen([
            OLD_QBITX, "-regtest",
            "-conf=%s" % (self.old_dir / "qbitx.conf"),
            "-datadir=%s" % self.old_dir,
            "-port=18870", "-rpcport=18871",
            "-rpcuser=qbx", "-rpcpassword=qbx",
            "-rpcbind=127.0.0.1", "-rpcallowip=127.0.0.1",
            "-listen=0", "-connect=0",
            "-dnsseed=0", "-discover=0", "-server=1",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.time() + 30
        while time.time() < deadline:
            if self.old_cli("getblockcount") is not None:
                return
            time.sleep(0.2)
        raise AssertionError("old node RPC did not start")

    def old_cli(self, *args):
        code, out = self.old_rpc(*args)
        if code != 0:
            return None
        return out.strip()

    def mine_empty(self):
        node = self.nodes[0]
        self.mock_time += 150
        node.setmocktime(self.mock_time)
        tmpl = node.getblocktemplate({"rules": ["segwit"]})
        raw = node.createrawtransaction([], {self.legacy: 1})
        spk = CScript(bytes.fromhex(node.decoderawtransaction(raw)["vout"][0]["scriptPubKey"]["hex"]))
        # Subsidy only. GBT coinbasevalue includes mempool fees for transactions
        # this empty block deliberately leaves out. Regtest reward is 12.5 QBX
        # and halves every 150 blocks.
        coinbase = create_coinbase(height=tmpl["height"], script_pubkey=spk)
        subsidy = ((1250 * COIN) // 100) >> (tmpl["height"] // 150)
        coinbase.vout[0].nValue = subsidy
        block = create_block(tmpl=tmpl, coinbase=coinbase, txlist=[])
        add_witness_commitment(block)
        solve_bounded(block)
        assert_equal(node.submitblock(block.serialize().hex()), None)

    def old_rpc(self, *args):
        # HTTP keeps an overweight block hex off the command line.
        body = json.dumps({"jsonrpc": "1.0", "id": "qbx", "method": args[0], "params": list(args[1:])}).encode()
        req = Request("http://127.0.0.1:18871/", data=body)
        req.add_header("Authorization", "Basic " + base64.b64encode(b"qbx:qbx").decode())
        req.add_header("Content-Type", "application/json")
        try:
            with urlopen(req, timeout=180) as resp:
                payload = json.loads(resp.read().decode())
        except HTTPError as exc:
            raw = exc.read().decode()
            try:
                err = json.loads(raw).get("error") or {}
                return 1, json.dumps(err)
            except json.JSONDecodeError:
                return 1, raw
        except URLError as exc:
            return 1, str(exc)
        if payload.get("error"):
            return 1, json.dumps(payload["error"])
        result = payload.get("result")
        if result is None:
            return 0, ""
        return 0, result if isinstance(result, str) else json.dumps(result)

    def stop_old(self):
        if getattr(self, "old", None) and self.old.poll() is None:
            self.old_rpc("stop")
            try:
                self.old.wait(timeout=20)
            except subprocess.TimeoutExpired:
                self.old.kill()

    def sync_old_clock(self):
        code, out = self.old_rpc("setmocktime", int(self.mock_time))
        assert code == 0, out

    def submit_chain_to_old(self, stop_hash):
        self.sync_old_clock()
        node = self.nodes[0]
        hashes = []
        current = stop_hash
        while True:
            header = node.getblockheader(current)
            hashes.append(current)
            if header["height"] == 0:
                break
            current = header["previousblockhash"]
        for blockhash in reversed(hashes):
            code, out = self.old_rpc("submitblock", node.getblock(blockhash, 0))
            assert code == 0, out
            assert out.strip() in ("", "duplicate", "duplicate-invalid"), out

    def run_test(self):
        assert Path(OLD_QBITX).is_file()
        node = self.nodes[0]
        self.mock_time = int(time.time())
        self.legacy = node.getnewaddress("", "dilithium-legacy")
        self.mine(121)
        native = node.getnewaddress("", "dilithium-bech32")
        assert native.startswith("rdil1")
        utxos = node.listunspent(100, 9999999, [self.legacy])
        assert_greater_than(len(utxos), 19)

        self.start_old()
        try:
            self.submit_chain_to_old(node.getbestblockhash())
            assert_equal(int(self.old_cli("getblockcount")), node.getblockcount())

            small_legacy = self.migration_hex(utxos[:1], native, legacy_change=True, fee=Decimal("0.05"))
            assert_equal(node.sendrawtransaction(small_legacy), node.decoderawtransaction(small_legacy)["txid"])
            small_native = self.migration_hex(utxos[1:2], native, fee=Decimal("0.05"))
            small_native_id = node.sendrawtransaction(small_native)
            assert_equal(small_native_id, node.decoderawtransaction(small_native)["txid"])
            for txid in (node.decoderawtransaction(small_legacy)["txid"], small_native_id):
                assert node.getmempoolentry(txid)["weight"] <= 400000

            legacy_hex = self.migration_hex(utxos[2:8], native, legacy_change=True)
            code, out = self.old_rpc("sendrawtransaction", legacy_hex)
            assert code != 0
            assert "tx-size" in out
            new_reject = node.testmempoolaccept([legacy_hex])[0]
            assert_equal(new_reject["allowed"], False)
            assert "tx-size" in new_reject["reject-reason"]

            first = self.migration_hex(utxos[8:14], native)
            second = self.migration_hex(utxos[14:20], native)
            code, out = self.old_rpc("sendrawtransaction", first)
            assert code != 0 and "tx-size" in out
            assert_equal(node.sendrawtransaction(first), node.decoderawtransaction(first)["txid"])
            assert_equal(node.sendrawtransaction(second), node.decoderawtransaction(second)["txid"])
            txid1 = node.decoderawtransaction(first)["txid"]
            txid2 = node.decoderawtransaction(second)["txid"]
            gbt = node.getblocktemplate({"rules": ["segwit"]})
            gbt_ids = [tx["txid"] for tx in gbt["transactions"]]
            assert txid1 in gbt_ids and txid2 in gbt_ids
            for entry in gbt["transactions"]:
                if entry["txid"] in (txid1, txid2):
                    mem = node.getmempoolentry(entry["txid"])
                    assert_equal(mem["weight"], entry["weight"])
                    assert_greater_than(entry["weight"], 400000)
                    assert entry["weight"] <= 1600000

            self.mine(1)
            block = node.getblock(node.getbestblockhash(), 1)
            assert txid1 in block["tx"] and txid2 in block["tx"]
            assert block["weight"] <= MAX_BLOCK_WEIGHT
            self.sync_old_clock()
            code, out = self.old_rpc("submitblock", node.getblock(block["hash"], 0))
            assert code == 0 and out.strip() == "", out
            assert_equal(int(self.old_cli("getblockcount")), node.getblockcount())

            tmpl = node.getblocktemplate({"rules": ["segwit"]})
            coinbase = create_coinbase(height=tmpl["height"])
            info_raw = node.createrawtransaction([], {self.legacy: 1})
            spk = bytes.fromhex(node.decoderawtransaction(info_raw)["vout"][0]["scriptPubKey"]["hex"])
            coinbase.vout[0].scriptPubKey = CScript(spk)
            coinbase.vout[0].nValue = tmpl["coinbasevalue"]
            filler = CScript(bytes([OP_TRUE]) * 10000)
            per_output = len(CTxOut(0, filler).serialize())
            n_out = ((MAX_BLOCK_WEIGHT // 16) + per_output) // per_output + 2
            for _ in range(n_out):
                coinbase.vout.append(CTxOut(0, filler))
            txs = [tx_from_hex(entry["data"]) for entry in tmpl["transactions"]]
            over = create_block(tmpl=tmpl, coinbase=coinbase, txlist=txs)
            add_witness_commitment(over)
            solve_bounded(over)
            rejected_new = node.submitblock(over.serialize().hex())
            assert rejected_new not in (None, "")
            self.sync_old_clock()
            code, out = self.old_rpc("submitblock", over.serialize().hex())
            assert code == 0
            assert out.strip() not in ("", "duplicate")
            assert_equal(node.getbestblockhash(), block["hash"])
            assert_equal(self.old_cli("getbestblockhash"), block["hash"])

            # A later height does not retire the policy. Restart reloads the tx.
            more = node.listunspent(1, 9999999, [self.legacy])
            mature = [u for u in more if u["confirmations"] >= 100]
            assert_greater_than(len(mature), 5)
            pending = self.migration_hex(mature[:6], native)
            pending_id = node.sendrawtransaction(pending)
            for _ in range(2):
                self.mine_empty()
            assert pending_id in node.getrawmempool()
            # Block timestamps are past wall-clock + 2h because each block steps
            # the LWMA spacing. -mocktime lets the restarted process load them.
            self.restart_node(0, extra_args=[
                "-conf=bitcoin.conf",
                "-persistmempool=1",
                "-mocktime=%d" % int(self.mock_time),
            ])
            node = self.nodes[0]
            assert pending_id in node.getrawmempool()
            assert node.getmempoolentry(pending_id)["weight"] > 400000
            assert node.getmempoolentry(pending_id)["weight"] <= 1600000
        finally:
            self.stop_old()


if __name__ == "__main__":
    MigrationPolicyTest(__file__).main()
