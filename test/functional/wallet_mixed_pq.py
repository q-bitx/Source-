#!/usr/bin/env python3
"""Focused standard RPC coverage for legacy/native PQ and ECDSA inputs."""
import time
from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.descriptors import descsum_create
from test_framework.wallet_util import get_generate_key
from test_framework.util import assert_equal, assert_raises_rpc_error
from test_framework.messages import CBlock, from_hex
from test_framework.test_node import ErrorMatch
from test_framework.script import CScript



class MixedPQTest(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-deprecatedrpc=settxfee"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def mine(self, count):
        for _ in range(count):
            self.mock_time += 150
            self.nodes[0].setmocktime(self.mock_time)
            self.generatetoaddress(self.nodes[0], 1, self.mining_address)

    def keep_only(self, addresses):
        node = self.nodes[0]
        node.lockunspent(True)
        utxos = node.listunspent(0)
        selected = [u for u in utxos if u.get("address") in addresses]
        assert_equal(len(selected), len(addresses))
        locked = [{"txid": u["txid"], "vout": u["vout"]}
                  for u in utxos if u.get("address") not in addresses]
        if locked:
            node.lockunspent(False, locked)
        return {(u["txid"], u["vout"]) for u in selected}

    def check_coinbase(self, script, expected):
        node = self.nodes[0]
        self.mock_time += 150
        node.setmocktime(self.mock_time)
        block = from_hex(CBlock(), node.generateblock(self.mining_address, [], False, called_by_framework=True)["hex"])
        block.vtx[0].vout[0].scriptPubKey = CScript(script)
        block.vtx[0].rehash()
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()
        assert_equal(node.submitblock(block.serialize().hex()), expected)

    def run_test(self):
        node = self.nodes[0]
        self.mock_time = int(time.time()) - 200000
        self.mining_address = node.getnewaddress("", "dilithium-legacy")
        assert not node.getnewaddress().startswith("rdil1")
        assert not node.getrawchangeaddress().startswith("rdil1")
        assert_raises_rpc_error(-32600, "not available", node.getnewaddress, "", "pq-bech32")
        for address_type in ("legacy", "bech32", "p2sh-segwit", "bech32m"):
            assert_raises_rpc_error(-8, "Only Dilithium", node.getnewaddress, "", address_type)
            assert_raises_rpc_error(-8, "Only Dilithium", node.getrawchangeaddress, address_type)
            node.getblockcount()  # The rejected request must not crash the node.
        self.mine(101)
        native = node.getnewaddress("", "pq-bech32")
        assert native.startswith("rdil1")
        assert node.getnewaddress().startswith("rdil1")
        assert node.getrawchangeaddress().startswith("rdil1")
        self.mine(17)
        assert_equal(node.getblockcount(), 118)
        script = bytes.fromhex(node.validateaddress(native)["scriptPubKey"])
        self.log.info("Native coinbase rejected before height 120; legacy remains accepted")
        assert_raises_rpc_error(-5, "Coinbase must use PQ", node.generatetoaddress, 1, native, called_by_framework=True)
        self.check_coinbase(script, "bad-cb-pq")
        key = get_generate_key()
        assert_raises_rpc_error(-8, "Classic multisig", node.createmultisig, 1, [key.pubkey])
        assert_raises_rpc_error(-8, "Classic multisig", node.addmultisigaddress, 1, [key.pubkey])
        assert_raises_rpc_error(-8, "Only Dilithium", node.deriveaddresses, descsum_create(f"addr({key.p2wpkh_addr})"))
        node.getblockcount()
        self.check_coinbase(bytes.fromhex(key.p2pkh_script), "bad-cb-pq")
        self.check_coinbase(script + b"\x00", "bad-cb-pq")
        self.check_coinbase(b"\x52\x4c\x14" + script[2:], "bad-cb-pq")
        for option in ("addresstype", "changetype"):
            self.stop_node(0)
            self.nodes[0].assert_start_raises_init_error(
                extra_args=[f"-{option}=legacy"], expected_msg="Only Dilithium", match=ErrorMatch.PARTIAL_REGEX)
            self.start_node(0)
        base_args = self.extra_args[0]
        self.restart_node(0, extra_args=base_args + [f"-mineraddress={native}"])
        assert_raises_rpc_error(-5, "Coinbase must use PQ", node.getblocktemplate, {"rules": ["segwit"]})
        self.restart_node(0, extra_args=base_args + [f"-minerdescriptor=raw({script.hex()})"])
        assert_raises_rpc_error(-5, "Coinbase must use PQ", node.getblocktemplate, {"rules": ["segwit"]})
        self.mine(1)
        assert_equal(node.getblockcount(), 119)
        self.log.info("Native coinbase accepted at activation via submitblock and mining RPCs")
        node.getblocktemplate({"rules": ["segwit"]})
        self.check_coinbase(script, None)
        assert_equal(node.getblockcount(), 120)
        self.check_coinbase(bytes.fromhex(key.p2pkh_script), "bad-cb-pq")
        self.check_coinbase(script + b"\x00", "bad-cb-pq")
        self.check_coinbase(b"\x51\x14" + script[2:], "bad-cb-pq")
        self.restart_node(0, extra_args=base_args + [f"-mineraddress={native}"])
        node.getblocktemplate({"rules": ["segwit"]})
        self.mock_time += 150
        node.setmocktime(self.mock_time)
        self.generatetoaddress(node, 1, native)
        self.mine(1)  # Legacy coinbase remains valid after activation.
        node.settxfee(Decimal("0.00005000"))
        groups = []
        for _ in range(2):
            key = get_generate_key()
            imported = node.importdescriptors([{"desc": descsum_create(f"wpkh({key.privkey})"), "timestamp": "now"}])
            assert imported[0]["success"]
            groups.append([node.getnewaddress("", "dilithium-legacy"),
                           node.getnewaddress("", "dilithium-bech32"), key.p2wpkh_addr])
        funding = {a: Decimal("1") for g in groups for a in g}
        self.log.info("Pure legacy PQ input: sendmany funds multiple PQ and ECDSA recipients")
        node.sendmany("", funding)
        self.mine(1)
        recipients = [node.getnewaddress("", t) for t in
                      ("dilithium-legacy", "dilithium-bech32")]
        for index, group in enumerate(groups):
            expected = self.keep_only(set(group))
            self.log.info("Mixed legacy PQ/native PQ/ECDSA inputs via %s",
                          "sendmany" if index else "sendtoaddress")
            if index:
                txid = node.sendmany("", {recipients[0]: Decimal("1.2"),
                                         recipients[1]: Decimal("1.3")})
            else:
                txid = node.sendtoaddress(recipients[1], Decimal("2.5"))
            tx = node.getrawtransaction(txid, True)
            assert_equal({(i["txid"], i["vout"]) for i in tx["vin"]}, expected)
            changes = [o["scriptPubKey"]["address"] for o in tx["vout"]
                       if o["scriptPubKey"].get("address") not in recipients]
            assert_equal(len(changes), 1)
            change = changes[0]
            assert node.getaddressinfo(change)["ismine"]
            assert change.startswith("rdil1")
            self.mine(1)
            self.keep_only({change})
            self.log.info("PQ witness change remains spendable with standard sendtoaddress")
            node.sendtoaddress(recipients[0], Decimal("0.1"))
            self.mine(1)
        node.lockunspent(True)


if __name__ == '__main__':
    MixedPQTest(__file__).main()
