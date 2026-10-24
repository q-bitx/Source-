#!/usr/bin/env python3
import time
from decimal import Decimal
from test_framework.test_framework import BitcoinTestFramework
from test_framework.messages import CTransaction, from_hex


class PQWeightRepro(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-debug=selectcoins", "-maxapsfee=-1"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        n = self.nodes[0]
        mining = n.getnewaddress("", "dilithium-legacy")
        now = int(time.time()) - 200000
        for _ in range(103):
            now += 150
            n.setmocktime(now)
            self.generatetoaddress(n, 1, mining)
        small = [n.getnewaddress("", "dilithium-legacy") for _ in range(12)]
        large = n.getnewaddress("", "dilithium-legacy")
        n.sendmany("", {**{a: Decimal("1") for a in small}, large: Decimal("10")}, fee_rate=5)
        now += 150
        n.setmocktime(now)
        self.generatetoaddress(n, 1, mining)
        locks = [{"txid": u["txid"], "vout": u["vout"]} for u in n.listunspent()
                 if u.get("address") not in small + [large]]
        n.lockunspent(False, locks)
        destinations = {n.getnewaddress(): Decimal("3"), n.getnewaddress(): Decimal("3")}
        large_coin = next(u for u in n.listunspent() if u.get("address") == large)
        txid = n.sendmany("", destinations, fee_rate=5)
        tx = n.getrawtransaction(txid, True)
        parsed = from_hex(CTransaction(), n.gettransaction(txid)["hex"])
        base = len(parsed.serialize_without_witness())
        weight = base * 16 + len(parsed.serialize_with_witness()) - base
        self.log.info("RESULT success inputs=%d actual_weight=%d vsize=%d", len(tx["vin"]), weight, (weight + 15) // 16)
        assert len(tx["vin"]) <= 4
        assert weight <= 400000
        assert any(i["txid"] == large_coin["txid"] and i["vout"] == large_coin["vout"] for i in tx["vin"])


if __name__ == '__main__':
    PQWeightRepro(__file__).main()
