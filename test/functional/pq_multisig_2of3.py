#!/usr/bin/env python3
# Copyright (c) 2025-present The Q-BitX Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test PQ multisig 2-of-3 spend on regtest.

This test verifies that a 2-of-3 Dilithium multisig scriptPubKey can be:
- Funded with a confirmed UTXO
- Spent using 2 valid PQ signatures (with dummy element)
- Accepted by mempool and confirmed in a block
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.messages import (
    CTransaction,
    CTxIn,
    CTxOut,
    COIN,
    tx_from_hex,
)
from test_framework.script import (
    CScript,
    OP_0,
    OP_2,
    OP_3,
    OP_CHECKMULTISIGDILITHIUM,
)
from test_framework.util import (
    assert_equal,
)
from test_framework.wallet import MiniWallet


class PQMultisig2of3Test(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-permitbaremultisig=1"]]  # Allow bare multisig outputs in regtest
        self.supports_cli = False

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        self.wallet = MiniWallet(node)
        
        self.log.info("Mine 101 blocks for coinbase maturity")
        self.generate(self.wallet, 101)
        
        self.log.info("Generate 3 PQ keypairs")
        # Generate 3 keypairs using pqkeypair RPC
        keypairs = []
        for i in range(3):
            kp = node.pqkeypair()
            keypairs.append({
                'pubkey': bytes.fromhex(kp['pubkey']),
                'privkey': bytes.fromhex(kp['privkey'])
            })
        
        self.log.info("Build 2-of-3 PQ multisig scriptPubKey")
        # scriptPubKey = <2> <pub1> <pub2> <pub3> <3> OP_CHECKMULTISIGDILITHIUM
        scriptPubKey = CScript([
            OP_2,
            keypairs[0]['pubkey'],
            keypairs[1]['pubkey'],
            keypairs[2]['pubkey'],
            OP_3,
            OP_CHECKMULTISIGDILITHIUM
        ])
        scriptPubKey_hex = scriptPubKey.hex()
        
        self.log.info("Create funding transaction")
        # Get a mature UTXO from MiniWallet
        funding_utxo = self.wallet.get_utxo()
        funding_txid = funding_utxo['txid']
        funding_vout = funding_utxo['vout']
        funding_amount = int(funding_utxo['value'] * COIN)
        
        # Create funding tx: spend wallet UTXO, output to multisig scriptPubKey
        funding_amount_sat = funding_amount - 1000  # Leave 1000 sat for fee
        funding_rawtx = node.createrawtransaction(
            inputs=[{'txid': funding_txid, 'vout': funding_vout}],
            outputs=[{'scriptPubKey': scriptPubKey_hex, 'amount': float(funding_amount_sat) / COIN}]
        )
        
        # Sign funding input with wallet
        funding_signed = node.signrawtransactionwithwallet(funding_rawtx)
        assert_equal(funding_signed['complete'], True)
        funding_tx_hex = funding_signed['hex']
        
        # Broadcast and mine funding tx
        funding_txid_final = node.sendrawtransaction(funding_tx_hex)
        self.generate(node, 1)
        
        # Verify funding tx is confirmed
        funding_tx_info = node.gettransaction(funding_txid_final)
        assert_equal(funding_tx_info['confirmations'], 1)
        
        self.log.info("Create spend transaction from multisig output")
        # Get the multisig UTXO
        multisig_utxo = node.gettxout(funding_txid_final, 0)
        assert_equal(multisig_utxo is not None, True)
        multisig_amount = int(multisig_utxo['value'] * COIN)
        
        # Create spend tx: input from multisig, output to new address
        spend_address = node.getnewaddress()
        spend_rawtx = node.createrawtransaction(
            inputs=[{'txid': funding_txid_final, 'vout': 0}],
            outputs={spend_address: float(multisig_amount - 1000) / COIN}
        )
        
        # Decode to get the transaction structure
        spend_tx_decoded = node.decoderawtransaction(spend_rawtx)
        spend_tx = tx_from_hex(spend_rawtx)
        
        self.log.info("Compute sighash and sign with 2 keys")
        # Compute sighash using pqsighash RPC (consistent with consensus)
        sighash_result = node.pqsighash(spend_rawtx, 0, scriptPubKey_hex, 1)  # 1 = SIGHASH_ALL
        sighash = bytes.fromhex(sighash_result['sighash'])
        
        # Sign with first 2 keys
        signatures = []
        for i in range(2):
            sig_result = node.pqsign(sighash.hex(), keypairs[i]['privkey'].hex())
            sig = bytes.fromhex(sig_result['sig'])
            # Append hashtype byte (SIGHASH_ALL = 1)
            sig_with_hashtype = sig + bytes([1])
            signatures.append(sig_with_hashtype)
        
        self.log.info("Build scriptSig: OP_0 <sig1> <sig2>")
        # scriptSig = OP_0 <sig1> <sig2> (CHECKMULTISIG dummy element + 2 signatures)
        scriptSig = CScript([OP_0] + signatures)
        scriptSig_hex = scriptSig.hex()
        
        # Update the transaction with scriptSig
        spend_tx.vin[0].scriptSig = scriptSig
        spend_tx_hex = spend_tx.serialize().hex()
        
        self.log.info("Test mempool acceptance")
        # Test mempool acceptance
        test_result = node.testmempoolaccept([spend_tx_hex])
        assert_equal(len(test_result), 1)
        assert_equal(test_result[0]['allowed'], True, 
                    f"Mempool rejected transaction: {test_result[0].get('reject-reason', 'unknown reason')}")
        
        self.log.info("Broadcast and mine spend transaction")
        # Broadcast spend tx
        spend_txid = node.sendrawtransaction(spend_tx_hex)
        
        # Mine block
        self.generate(node, 1)
        
        self.log.info("Verify transaction is confirmed")
        # Verify tx is confirmed
        spend_tx_info = node.gettransaction(spend_txid)
        assert_equal(spend_tx_info['confirmations'], 1)
        assert 'blockhash' in spend_tx_info, "Transaction should be in a block"
        
        # Verify via getrawtransaction
        raw_tx = node.getrawtransaction(spend_txid, True)
        assert 'blockhash' in raw_tx, "Transaction should be in a block"
        assert_equal(raw_tx['confirmations'], 1)
        
        self.log.info("PQ multisig 2-of-3 spend test passed")


if __name__ == '__main__':
    PQMultisig2of3Test().main()
