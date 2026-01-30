#!/usr/bin/env python3
# Copyright (c) 2025-present The Q-BitX Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test Dilithium P2DPKH address generation and persistence."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)

class WalletDilithiumTest(BitcoinTestFramework):
    def add_options(self, parser):
        self.add_wallet_options(parser)

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.supports_cli = False

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        
        self.log.info("Test getnewaddress with pq alias")
        # Create a new legacy wallet (descriptors=false)
        node.createwallet(wallet_name="test_dilithium", blank=True, descriptors=False)
        wallet = node.get_wallet_rpc("test_dilithium")
        
        # Generate a Dilithium address using "pq" alias
        addr_pq = wallet.getnewaddress("", "pq")
        self.log.info(f"Generated address with 'pq': {addr_pq}")
        
        # Generate a Dilithium address using canonical name
        addr_canonical = wallet.getnewaddress("", "dilithium-legacy")
        self.log.info(f"Generated address with 'dilithium-legacy': {addr_canonical}")
        
        # Verify addresses are valid base58 Dilithium addresses
        assert_equal(len(addr_pq), 34)  # Base58 Dilithium addresses are 34 chars
        assert_equal(len(addr_canonical), 34)
        
        # Test getaddressinfo for pq address
        self.log.info("Test getaddressinfo for Dilithium address")
        info_pq = wallet.getaddressinfo(addr_pq)
        assert_equal(info_pq["ismine"], True)
        assert "scriptPubKey" in info_pq
        assert_equal(info_pq["scriptPubKey"][-2:], "c4")  # OP_CHECKSIGDILITHIUM = 0xC4
        
        # Check that script type is reported correctly via decodescript
        decoded = wallet.decodescript(info_pq["scriptPubKey"])
        assert_equal(decoded["type"], "dilithium_pubkeyhash")
        
        # Test persistence: unload and reload wallet
        self.log.info("Test wallet persistence")
        node.unloadwallet("test_dilithium")
        node.loadwallet("test_dilithium")
        wallet = node.get_wallet_rpc("test_dilithium")
        
        # Verify address is still recognized as ours
        info_after_reload = wallet.getaddressinfo(addr_pq)
        assert_equal(info_after_reload["ismine"], True)
        assert_equal(info_after_reload["scriptPubKey"], info_pq["scriptPubKey"])
        
        # Test mining to the address
        self.log.info("Test mining to Dilithium address")
        initial_balance = wallet.getbalances()["mine"]["immature"]
        node.generatetoaddress(1, addr_pq)
        self.sync_all()
        new_balance = wallet.getbalances()["mine"]["immature"]
        assert_equal(new_balance > initial_balance, True, "Immature balance should increase after mining")
        
        # Test that descriptor wallets are rejected
        self.log.info("Test that descriptor wallets reject pq addresses")
        node.createwallet(wallet_name="test_descriptor", blank=True, descriptors=True)
        descriptor_wallet = node.get_wallet_rpc("test_descriptor")
        assert_raises_rpc_error(-4, "pq address type is not supported for descriptor wallets yet; createwallet with descriptors=false",
                                descriptor_wallet.getnewaddress, "", "pq")

if __name__ == '__main__':
    WalletDilithiumTest().main()
