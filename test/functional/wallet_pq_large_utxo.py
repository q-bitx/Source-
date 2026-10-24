#!/usr/bin/env python3
"""PQ coin selection at scale, four address paths, and two-node relay."""
import math
import importlib.util
import subprocess
import time
from decimal import Decimal

from test_framework.authproxy import JSONRPCException
from test_framework.messages import CTransaction, CTxOut, from_hex
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class LargePQTest(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)
        parser.add_argument("--utxo-count", type=int, default=50001)
        parser.add_argument("--payout-engine", default="")

    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [["-maxapsfee=-1", "-persistmempool=0", "-testactivationheight=pq_witness@120",
                            "-debugexclude=walletdb"] for _ in range(2)]
        self.rpc_timeout = 180

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def tick(self, seconds=150):
        self.now += seconds
        for n in self.nodes:
            n.setmocktime(self.now)

    def mine(self, count=1, empty=False):
        for _ in range(count):
            self.tick()
            if empty:
                self.generateblock(self.nodes[0], self.mining, [])
            else:
                self.generatetoaddress(self.nodes[0], 1, self.mining)

    def check_tx(self, txid):
        n = self.nodes[0]
        tx = from_hex(CTransaction(), n.gettransaction(txid)["hex"])
        base = len(tx.serialize_without_witness())
        total = len(tx.serialize_with_witness())
        scale = 16 if n.getblockcount() + 1 >= 120 else 4
        weight = base * scale + total - base
        vsize = math.ceil(weight / scale)
        assert weight <= 400000, (len(tx.vin), weight)
        fee = -n.gettransaction(txid)["fee"]
        assert fee >= Decimal(5 * vsize) / 100000000, (fee, vsize)
        assert fee <= Decimal(5 * (vsize + 2)) / 100000000, (fee, vsize)
        self.tick(60)  # Advance the P2P inventory trickle timer under mocktime.
        self.sync_mempools()
        assert txid in self.nodes[1].getrawmempool()
        assert txid in {t["txid"] for t in self.nodes[1].getblocktemplate({"rules": ["segwit"]})["transactions"]}
        self.log.info("PASS inputs=%d outputs=%d actual_weight=%d fee=%s", len(tx.vin), len(tx.vout), weight, fee)
        return tx

    def keep_only(self, addresses):
        n = self.nodes[0]
        n.lockunspent(True)
        locks = [{"txid": u["txid"], "vout": u["vout"]} for u in n.listunspent()
                 if u.get("address") not in addresses]
        if locks:
            n.lockunspent(False, locks)

    def run_test(self):
        n, peer = self.nodes
        self.now = int(time.time())
        self.mining = n.getnewaddress("", "dilithium-legacy")
        self.mine(103)
        before = n.sendtoaddress(peer.getnewaddress("", "dilithium-legacy"), Decimal("0.1"), fee_rate=5)
        self.check_tx(before)
        self.mine(16, empty=True)
        assert_equal(n.getblockcount(), 119)
        assert before in peer.getrawmempool()
        self.mine(11)
        legacy = n.getnewaddress("", "dilithium-legacy")
        native = n.getnewaddress("", "pq-bech32")
        seed = n.sendtoaddress(legacy, Decimal("35"), fee_rate=5)
        self.check_tx(seed)
        self.mine()
        utxo = next(u for u in n.listunspent() if u.get("address") == legacy)
        source_txid, source_vout, balance = utxo["txid"], utxo["vout"], utxo["amount"]
        amount = Decimal("0.0005")
        scripts = {a: bytes.fromhex(n.validateaddress(a)["scriptPubKey"]) for a in (legacy, native, self.mining)}
        made = 0
        while made < self.options.utxo_count:
            count = min(500, self.options.utxo_count - made)
            outputs = [{legacy if i % 2 else native: amount} for i in range(count)]
            change = balance - amount * count - Decimal("0.002")
            outputs.append({self.mining: change})
            inputs = [{"txid": source_txid, "vout": source_vout}]
            tx = from_hex(CTransaction(), n.createrawtransaction(inputs, {self.mining: change}))
            tx.vout = [CTxOut(int(amount * 100000000), scripts[legacy if i % 2 else native]) for i in range(count)]
            tx.vout.append(CTxOut(int(change * 100000000), scripts[self.mining]))
            signed = n.signrawtransactionwithwallet(tx.serialize().hex())
            assert signed["complete"]
            fee = Decimal(len(bytes.fromhex(signed["hex"])) * 5) / 100000000
            change = balance - amount * count - fee
            tx.vout[-1].nValue = int(change * 100000000)
            signed = n.signrawtransactionwithwallet(tx.serialize().hex())
            assert signed["complete"]
            source_txid = n.sendrawtransaction(signed["hex"])
            source_vout, balance = count, change
            made += count
            if made % 2000 == 0 or made == self.options.utxo_count:
                self.mine()
                self.log.info("Funded %d UTXOs", made)
        # Include the remainder transaction if the final checkpoint did not mine it.
        self.mine()
        assert len(n.listunspent()) >= self.options.utxo_count
        for recipients in (1, 2, 10, 100):
            receiver_balance = peer.getbalance()
            outputs = {peer.getnewaddress("", "dilithium-legacy" if i % 2 else "pq-bech32"): Decimal("0.01")
                       for i in range(recipients)}
            txid = (n.sendtoaddress(next(iter(outputs)), Decimal("0.01"), fee_rate=5) if recipients == 1
                    else n.sendmany("", outputs, fee_rate=5))
            self.check_tx(txid)
            self.mine()
            assert_equal(peer.getbalance(), receiver_balance + sum(outputs.values()))
        self.keep_only({legacy})
        balance_before = n.getbalance()
        assert_raises_rpc_error(-6, None, n.sendtoaddress, peer.getnewaddress(), Decimal("0.01"), fee_rate=5)
        assert_equal(n.getbalance(), balance_before)
        n.lockunspent(True)
        # Four forced source/destination combinations, excluding unrelated coins.
        for source_witness in (False, True):
            source = n.getnewaddress("", "pq-bech32" if source_witness else "dilithium-legacy")
            for witness in (False, True):
                n.lockunspent(True)
                n.sendtoaddress(source, Decimal("1"), fee_rate=5)
                self.mine()
                self.keep_only({source})
                dest = peer.getnewaddress("", "pq-bech32" if witness else "dilithium-legacy")
                receiver_balance = peer.getbalance()
                txid = n.sendtoaddress(dest, Decimal("0.1"), fee_rate=5)
                self.check_tx(txid)
                self.mine()
                assert_equal(peer.getbalance(), receiver_balance + Decimal("0.1"))
        n.lockunspent(True)
        self.disconnect_nodes(0, 1)
        txid = n.sendtoaddress(peer.getnewaddress(), Decimal("0.1"), fee_rate=5)
        self.tick(600)
        self.generateblock(n, self.mining, [], sync_fun=self.no_op)
        self.restart_node(0, extra_args=self.extra_args[0] + [f"-mocktime={self.now}"])
        n = self.nodes[0]
        assert txid in n.getrawmempool()
        # Wallet rebroadcast intentionally waits for a newly connected block
        # after startup; its last-block timer starts at zero on reload.
        self.tick(150)
        self.generateblock(n, self.mining, [], sync_fun=self.no_op)
        self.connect_nodes(0, 1)
        self.sync_blocks()
        self.tick(36 * 3600 + 600)
        n.mockscheduler(60)
        self.tick(600)
        def received_rebroadcast():
            if txid in peer.getrawmempool():
                return True
            self.tick(1)  # Allow the inbound peer's transaction request delay to expire.
            return False

        self.wait_until(received_rebroadcast, timeout=30)
        self.log.info("PASS wallet restart and automatic rebroadcast to second node")
        if self.options.payout_engine:
            # The framework starts nodes with bitcoin.conf; the real CLI reads qbitx.conf.
            (n.datadir_path / "qbitx.conf").write_text((n.datadir_path / "bitcoin.conf").read_text())
            spec = importlib.util.spec_from_file_location("payout_engine", self.options.payout_engine)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            cfg = {"pool_address": self.mining, "qbitx_cli": self.binary_paths.bitcoincli,
                   "chain": "regtest", "datadir": str(n.datadir_path), "rpcwallet": n.listwallets()[0],
                   "ledger_path": str(n.datadir_path / "test-payout.sqlite"),
                   "dry_run": False, "live_payouts_enabled": True, "payout_rpc": "sendmany"}
            engine = module.Engine(cfg)
            round_id = engine.ledger.create_round("ab" * 32, 1, 3000000, 111,
                [(peer.getnewaddress("", "dilithium-legacy"), Decimal("0.01")),
                 (peer.getnewaddress("", "pq-bech32"), Decimal("0.02"))])
            send_many = engine._send_many

            def lost_response(pays, batch_id):
                send_many(pays, batch_id)
                raise subprocess.TimeoutExpired("sendmany", 60)

            engine._send_many = lost_response
            engine._pay_many(engine.ledger.payments_for_round(round_id))
            assert all(r["status"] == "uncertain" for r in engine.ledger.payments_for_round(round_id))
            engine.ledger.conn.close()
            engine = module.Engine(cfg)
            engine._pay_many(engine.ledger.payments_for_round(round_id))
            rows = engine.ledger.payments_for_round(round_id)
            assert all(r["status"] == "pending" for r in rows), [dict(r) for r in rows]
            assert_equal(len({r["txid"] for r in rows}), 1)
            payout_txid = rows[0]["txid"]
            engine._pay_many(rows)
            assert_equal({r["txid"] for r in engine.ledger.payments_for_round(round_id)}, {payout_txid})
            self.tick(60)
            self.mine()
            engine._pay_many(engine.ledger.payments_for_round(round_id))
            assert all(r["status"] == "confirmed" for r in engine.ledger.payments_for_round(round_id))
            engine.ledger.conn.close()
            self.log.info("PASS real batch RPC timeout, payout restart, comment recovery, and no duplicate payment")


if __name__ == '__main__':
    LargePQTest(__file__).main()
