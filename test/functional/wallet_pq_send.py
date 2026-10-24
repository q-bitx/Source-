#!/usr/bin/env python3
"""Regression for native PQ signing through send's PSBT path."""
import json
import time
from decimal import Decimal
from pathlib import Path

from test_framework.authproxy import JSONRPCException
from test_framework.descriptors import descsum_create
from test_framework.messages import CTransaction, from_hex
from test_framework.psbt import PSBT, PSBT_IN_FINAL_SCRIPTWITNESS, PSBT_IN_NON_WITNESS_UTXO
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.wallet_util import get_generate_key


class NativePQSendTest(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)
        parser.add_argument("--expect-incomplete", action="store_true")
        parser.add_argument("--evidence", default=None)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-maxapsfee=-1", "-keypool=2"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def mine(self, count):
        n = self.nodes[0]
        for _ in range(count):
            self.mock_time += 150
            n.setmocktime(self.mock_time)
            self.generatetoaddress(n, 1, self.mining)

    def record(self, name, **data):
        self.results.append(dict(test=name, **data))
        self.log.info("RESULT %s %s", name, json.dumps(data, default=str))
        if self.options.evidence:
            Path(self.options.evidence).write_text(json.dumps(self.results, indent=2, default=str))

    def reject(self, name, fn):
        try:
            result = fn()
        except JSONRPCException as exc:
            self.record(name, rejected=True, error=exc.error)
            return
        assert result.get("complete") is False, (name, {k: v for k, v in result.items() if k not in ("hex", "psbt")})
        assert "hex" not in result and "txid" not in result
        self.record(name, rejected=True, complete=False)

    def coins(self, addresses):
        coins = [u for u in self.nodes[0].listunspent() if u.get("address") in addresses]
        assert_equal(len(coins), len(addresses))
        return coins

    def prepare(self, coins, outputs, change, **options):
        opts = dict(inputs=[dict(txid=u["txid"], vout=u["vout"]) for u in coins],
                    add_inputs=False, add_to_wallet=False, change_address=change,
                    change_position=len(outputs), locktime=self.nodes[0].getblockcount())
        opts.update(options)
        return self.nodes[0].send(outputs, fee_rate=5, options=opts)

    def verify(self, name, result, coins, outputs, change, broadcast=True):
        n = self.nodes[0]
        assert_equal(result["complete"], True)
        tx = n.decoderawtransaction(result["hex"])
        assert_equal(tx["txid"], result["txid"])
        assert_equal({(i["txid"], i["vout"]) for i in tx["vin"]},
                     {(u["txid"], u["vout"]) for u in coins})
        actual = {}
        for out in tx["vout"]:
            addr = out["scriptPubKey"]["address"]
            actual[addr] = actual.get(addr, Decimal(0)) + out["value"]
        for addr, value in outputs.items():
            assert_equal(actual.pop(addr), value)
        assert_equal(set(actual), {change})
        fee = sum(u["amount"] for u in coins) - sum(o["value"] for o in tx["vout"])
        parsed = from_hex(CTransaction(), result["hex"])
        base = len(parsed.serialize_without_witness())
        weight = base * 16 + len(parsed.serialize_with_witness()) - base
        vsize = (weight + 15) // 16
        assert weight <= 400000
        # decoderawtransaction's context-free serializer reports Bitcoin k=4.
        rpc_weight = base * 4 + len(parsed.serialize_with_witness()) - base
        assert_equal(tx["vsize"], (rpc_weight + 3) // 4)
        assert_equal(fee, Decimal(vsize * 5) / Decimal(100000000))
        accepted = n.testmempoolaccept([result["hex"]])[0]
        assert_equal(accepted["allowed"], True)
        assert_equal(accepted["fees"]["base"], fee)
        if broadcast:
            assert_equal(n.sendrawtransaction(result["hex"]), result["txid"])
            self.mine(1)
            assert n.gettransaction(result["txid"])["confirmations"] > 0
        self.record(name, txid=result["txid"], inputs=len(tx["vin"]),
                    recipients=len(outputs), outputs=len(tx["vout"]), weight_k16=weight,
                    vsize=vsize, fee=fee, confirmed=broadcast)
        return parsed

    def integration(self):
        n = self.nodes[0]
        groups = [[n.getnewaddress("", "pq-bech32") for _ in range(k)] for k in (1, 1, 3, 3)]
        capacity = [n.getnewaddress("", "pq-bech32") for _ in range(72)]
        legacy = n.getnewaddress("", "dilithium-legacy")
        mixed = [n.getnewaddress("", "pq-bech32"), n.getnewaddress("", "dilithium-legacy")]
        key = get_generate_key()
        assert n.importdescriptors([dict(desc=descsum_create(f"wpkh({key.privkey})"), timestamp="now")])[0]["success"]
        mixed.append(key.p2wpkh_addr)
        other = [n.getnewaddress("", "pq-bech32") for _ in range(3)]
        funding = {a: Decimal("0.1") for g in groups for a in g}
        funding.update({a: Decimal("0.01") for a in capacity})
        funding.update({a: Decimal("0.1") for a in [legacy, *mixed, *other]})
        n.sendmany("", funding, fee_rate=5)
        self.mine(1)
        destinations = [n.getnewaddress("", "pq-bech32" if i % 2 else "dilithium-legacy") for i in range(10)]
        for count, group in zip((1, 2, 5, 10), groups):
            coins = self.coins(group)
            outputs = {a: Decimal("0.001") for a in destinations[:count]}
            result = self.prepare(coins, outputs, group[0])
            # A real bad signature must be rejected by the unchanged mempool verifier.
            if count == 1:
                bad = from_hex(CTransaction(), result["hex"])
                sig = bytearray(bad.wit.vtxinwit[0].scriptWitness.stack[0])
                sig[10] ^= 1
                bad.wit.vtxinwit[0].scriptWitness.stack[0] = bytes(sig)
                rejected = n.testmempoolaccept([bad.serialize().hex()])[0]
                assert_equal(rejected["allowed"], False)
                self.record("invalid_native_signature", rejection=rejected)
            self.verify(f"native_send_{count}", result, coins, outputs, group[0])

        coins = self.coins([legacy])
        outputs = {destinations[0]: Decimal("0.02")}
        self.verify("legacy_send", self.prepare(coins, outputs, legacy), coins, outputs, legacy)
        coins = self.coins(mixed)
        outputs = {destinations[1]: Decimal("0.25")}
        self.verify("native_legacy_ecdsa_send", self.prepare(coins, outputs, mixed[0]), coins, outputs, mixed[0])

        coins = self.coins([other[0]])
        outputs = {destinations[0]: Decimal("0.02")}
        self.reject("insufficient_balance", lambda: self.prepare(coins, {destinations[0]: Decimal("1000000")}, other[0]))
        self.reject("invalid_address", lambda: self.prepare(coins, {"not-an-address": Decimal("0.01")}, other[0]))
        self.reject("fee_below_relay", lambda: n.send(outputs, fee_rate=0, options=dict(
            inputs=[dict(txid=u["txid"], vout=u["vout"]) for u in coins], add_inputs=False,
            add_to_wallet=False, change_address=other[0])))
        unsigned = n.createpsbt([dict(txid=u["txid"], vout=u["vout"]) for u in coins], outputs)
        unsigned = n.walletprocesspsbt(unsigned, False)["psbt"]
        n.createwallet("pq-send-no-keys", disable_private_keys=True, blank=True)
        watch = n.get_wallet_rpc("pq-send-no-keys")
        missing = watch.walletprocesspsbt(unsigned)
        assert_equal(missing["complete"], False)
        assert_equal(n.finalizepsbt(missing["psbt"])["complete"], False)
        self.record("missing_private_key", complete=False)
        n.unloadwallet("pq-send-no-keys")
        result = self.prepare(coins, outputs, other[0])
        psbt = PSBT.from_base64(result["psbt"])
        assert PSBT_IN_FINAL_SCRIPTWITNESS in psbt.i[0].map
        # Independent finalizer must preserve the already verified native witness.
        final = n.finalizepsbt(result["psbt"])
        assert_equal(final["complete"], True)
        assert_equal(final["hex"], result["hex"])
        bad_psbt = PSBT.from_base64(result["psbt"])
        bad_witness = bytearray(bad_psbt.i[0].map[PSBT_IN_FINAL_SCRIPTWITNESS])
        bad_witness[15] ^= 1
        bad_psbt.i[0].map[PSBT_IN_FINAL_SCRIPTWITNESS] = bytes(bad_witness)
        invalid_final = n.finalizepsbt(bad_psbt.to_base64())
        assert_equal(invalid_final["complete"], False)
        self.record("invalid_native_psbt_signature", complete=False)
        bad_psbt.i[0].map[PSBT_IN_NON_WITNESS_UTXO] = bytes.fromhex(n.gettransaction(coins[0]["txid"])["hex"])
        invalid_final = n.finalizepsbt(bad_psbt.to_base64())
        assert_equal(invalid_final["complete"], False)
        self.record("invalid_native_psbt_with_full_prevtx", complete=False)
        self.verify("native_psbt_finalize", result, coins, outputs, other[0])
        duplicate = self.prepare(coins, outputs, other[0])
        rejected = n.testmempoolaccept([duplicate["hex"]])[0]
        assert_equal(rejected["allowed"], False)
        try:
            n.sendrawtransaction(duplicate["hex"])
        except JSONRPCException as exc:
            self.record("double_spend", rejection=rejected, broadcast_error=exc.error)
        else:
            raise AssertionError("Spent inputs were accepted")

        # A freshly mined native coinbase is unavailable to coin selection.
        self.mock_time += 150
        n.setmocktime(self.mock_time)
        block = self.generatetoaddress(n, 1, other[1])[0]
        cb = n.getblock(block, 2)["tx"][0]
        immature = n.send(outputs, fee_rate=5, options=dict(
            inputs=[dict(txid=cb["txid"], vout=0)], add_inputs=False, add_to_wallet=False,
            change_address=other[1]))
        rejected = n.testmempoolaccept([immature["hex"]])[0]
        assert_equal(rejected["allowed"], False)
        assert "premature" in rejected["reject-reason"]
        try:
            n.sendrawtransaction(immature["hex"])
        except JSONRPCException as exc:
            assert "premature" in exc.error["message"]
            self.record("immature_coinbase", rejection=rejected, broadcast_error=exc.error)
        else:
            raise AssertionError("Immature coinbase was accepted")

        coins = self.coins(capacity)
        outputs = {a: Decimal("0.001") for a in destinations}
        low, high = 1, len(coins) + 1
        last = None
        while low + 1 < high:
            mid = (low + high) // 2
            try:
                candidate = self.prepare(coins[:mid], outputs, capacity[0])
            except JSONRPCException as exc:
                assert "weight" in exc.error["message"].lower() or "large" in exc.error["message"].lower(), exc.error
                high = mid
            else:
                assert_equal(candidate["complete"], True)
                low, last = mid, candidate
        assert last is not None and high <= len(coins)
        self.reject("native_weight_boundary", lambda: self.prepare(coins[:high], outputs, capacity[0]))
        last = self.prepare(coins[:low], outputs, capacity[0])
        self.verify("native_capacity_max_inputs", last, coins[:low], outputs, capacity[0])
        self.record("native_input_capacity_boundary", accepted=low, rejected=high)

        # Measure output capacity independently, with one native input and native change.
        output_addresses = [n.getnewaddress("", "pq-bech32") for _ in range(800)]
        coins = self.coins([other[0]])
        low, high = 1, len(output_addresses)
        while low + 1 < high:
            mid = (low + high) // 2
            outputs = {a: Decimal("0.00002") for a in output_addresses[:mid]}
            try:
                candidate = self.prepare(coins, outputs, other[0])
            except JSONRPCException as exc:
                assert "weight" in exc.error["message"].lower() or "large" in exc.error["message"].lower(), exc.error
                high = mid
            else:
                assert_equal(candidate["complete"], True)
                low = mid
        outputs = {a: Decimal("0.00002") for a in output_addresses[:high]}
        self.reject("native_output_weight_boundary", lambda: self.prepare(coins, outputs, other[0]))
        outputs = {a: Decimal("0.00002") for a in output_addresses[:low]}
        self.verify("native_capacity_max_outputs", self.prepare(coins, outputs, other[0]),
                    coins, outputs, other[0])
        self.record("native_output_capacity_boundary", accepted_recipients=low, rejected_recipients=high)

        # Reload both wallet and node; use a preselected unspent native coin after each.
        for kind in ("wallet", "node"):
            coins = self.coins([other[2]])
            outputs = {destinations[0]: Decimal("0.001")}
            before_restart = self.prepare(coins, outputs, other[2])
            wallet_name = n.getwalletinfo()["walletname"]
            if kind == "wallet":
                n.unloadwallet(wallet_name)
                n.loadwallet(wallet_name)
            else:
                self.restart_node(0)
                n = self.nodes[0]
                n.setmocktime(self.mock_time)
            coins = self.coins([other[2]])
            outputs = {destinations[0]: Decimal("0.001")}
            result = self.prepare(coins, outputs, other[2])
            assert_equal(result["txid"], before_restart["txid"])
            self.verify("native_after_" + kind + "_restart", result, coins, outputs, other[2])

        # Legacy RPC continues to sign native inputs with no implicit broadcast.
        prepared = n.pqsendfrom(other[2], destinations[1], Decimal("0.001"), "low", "prepare")
        assert_equal(prepared["broadcast"], False)
        assert_equal(n.testmempoolaccept([prepared["rawtx"]])[0]["allowed"], True)
        assert_equal(n.sendrawtransaction(prepared["rawtx"]), prepared["txid"])
        self.mine(1)
        assert n.gettransaction(prepared["txid"])["confirmations"] > 0
        self.record("pqsendfrom_prepare_regression", txid=prepared["txid"], confirmed=True)

        # The standard send commit/broadcast mode also spends a mature native coinbase.
        self.mine(100)
        cbcoin = next(u for u in n.listunspent() if u["txid"] == cb["txid"] and u["vout"] == 0)
        assert cbcoin["confirmations"] >= 100
        result = n.send({destinations[0]: Decimal("0.02")}, fee_rate=5, options=dict(
            inputs=[dict(txid=cb["txid"], vout=0)], add_inputs=False, change_address=other[1]))
        assert_equal(result["complete"], True)
        assert "hex" not in result  # Default send commits and returns TXID, per its API.
        assert result["txid"] in n.getrawmempool()
        self.mine(1)
        assert n.gettransaction(result["txid"])["confirmations"] > 0
        self.record("native_mature_coinbase_default_broadcast", txid=result["txid"], confirmed=True)

    def run_test(self):
        self.results = []
        n = self.nodes[0]
        self.mock_time = int(time.time()) - 200000
        self.mining = n.getnewaddress("", "dilithium-legacy")
        self.mine(130)
        native = n.getnewaddress("", "pq-bech32")
        dest = n.getnewaddress("", "pq-bech32")
        n.sendtoaddress(native, Decimal("2"), fee_rate=5)
        self.mine(1)
        coin = next(u for u in n.listunspent() if u.get("address") == native)
        opts = dict(inputs=[dict(txid=coin["txid"], vout=coin["vout"])],
                    add_inputs=False, add_to_wallet=False, change_address=native)
        result = n.send({dest: Decimal("0.5")}, fee_rate=5, options=opts)
        self.log.info("SEND RESULT complete=%s hex=%s txid=%s psbt=%s",
                      result["complete"], "hex" in result, result.get("txid"),
                      "psbt" in result)
        if self.options.expect_incomplete:
            assert_equal(result["complete"], False)
            assert "hex" not in result and "txid" not in result
            prepare = n.pqsendfrom(native, dest, Decimal("0.5"), "low", "prepare")
            assert_equal(prepare["broadcast"], False)
            assert_equal(n.testmempoolaccept([prepare["rawtx"]])[0]["allowed"], True)
            assert_equal(n.sendrawtransaction(prepare["rawtx"]), prepare["txid"])
            self.mine(1)
            assert n.gettransaction(prepare["txid"])["confirmations"] > 0
            self.log.info("SAME NATIVE KEY/UTXO: pqsendfrom prepare signs and confirms; send fails")
            return
        assert_equal(result["complete"], True)
        assert result["hex"] and result["txid"]
        assert_equal(n.testmempoolaccept([result["hex"]])[0]["allowed"], True)
        assert_equal(n.sendrawtransaction(result["hex"]), result["txid"])
        self.mine(1)
        assert n.gettransaction(result["txid"])["confirmations"] > 0
        self.record("native_initial_send", txid=result["txid"], confirmed=True)
        self.integration()


if __name__ == '__main__':
    NativePQSendTest(__file__).main()
