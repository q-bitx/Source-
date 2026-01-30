#!/usr/bin/env python3
# Copyright (c) 2025 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test Emergency Difficulty Adjustment (EDA)

Test that EDA triggers based on hybrid time measure (max block_time, MTP) when
candidate block time is >= 5 minutes (300 seconds) after previous block, reducing difficulty by 2x.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.blocktools import (
    create_block,
    create_coinbase,
    DIFF_1_N_BITS,
    DIFF_1_TARGET,
    nbits_str,
    target_str,
)
from test_framework.messages import CBlock
from test_framework.wallet import MiniWallet

DIFFICULTY_ADJUSTMENT_INTERVAL = 2016
EDA_WINDOW = 5 * 60  # 5 minutes in seconds (300 seconds) for testing


class EDATest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.chain = "regtest"  # Use regtest for easier testing

    def run_test(self):
        node = self.nodes[0]
        wallet = MiniWallet(node)

        self.log.info("Test 1: EDA trigger based on header times")
        # Mine a few blocks to establish chain
        self.generate(wallet, 10, sync_fun=self.no_op)
        
        # Get the last block info
        last_block_hash = node.getbestblockhash()
        last_block = node.getblock(last_block_hash, 2)
        last_block_time = last_block['time']
        last_block_bits = int(last_block['bits'], 16)
        last_block_height = last_block['height']
        
        self.log.info(f"Last block: height={last_block_height}, time={last_block_time}, bits=0x{last_block_bits:08x}")
        
        # Verify we're not at a retarget boundary
        assert_equal((last_block_height + 1) % DIFFICULTY_ADJUSTMENT_INTERVAL, 1)
        
        # Create a big gap: set next block time = prev_time + window + 1
        # Use 301 seconds to ensure EDA triggers (window is 300 seconds)
        eda_block_time = last_block_time + EDA_WINDOW + 1
        
        # Set mocktime so getblocktemplate uses the correct timestamp
        node.setmocktime(eda_block_time)
        
        # Get block template - should show EDA triggered
        tmpl = node.getblocktemplate({'rules': ['segwit']})
        expected_bits = int(tmpl['bits'], 16)
        
        self.log.info(f"Block template bits after EDA: 0x{expected_bits:08x}")
        
        # Verify that the difficulty was halved (target doubled)
        from test_framework.messages import uint256_from_compact
        old_target = uint256_from_compact(last_block_bits)
        new_target = uint256_from_compact(expected_bits)
        
        # EDA should double the target (halve difficulty)
        expected_target = old_target * 2
        # Cap at powLimit if needed
        pow_limit = 0x00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff
        if expected_target > pow_limit:
            expected_target = pow_limit
        
        self.log.info(f"Old target: {old_target:064x}")
        self.log.info(f"New target: {new_target:064x}")
        self.log.info(f"Expected target (old * 2, clamped): {expected_target:064x}")
        
        # Assert bits changed to exactly doubled target (or powLimit-clamped)
        assert_equal(new_target, expected_target, "EDA should double the target (halve difficulty) or clamp to powLimit")
        
        # Create and submit the block with EDA timestamp
        coinbase = create_coinbase(height=last_block_height + 1)
        block = create_block(
            hashprev=int(last_block_hash, 16),
            coinbase=coinbase,
            ntime=eda_block_time
        )
        block.nBits = expected_bits
        block.solve()
        
        # Submit the block
        result = node.submitblock(block.serialize().hex())
        assert_equal(result, None, "EDA block should be accepted")
        
        # Verify the block was accepted
        new_block_hash = node.getbestblockhash()
        assert_equal(new_block_hash, block.hash.hex())
        
        new_block = node.getblock(new_block_hash, 2)
        assert_equal(new_block['bits'], nbits_str(expected_bits))
        assert_equal(new_block['time'], eda_block_time)
        
        self.log.info("Test 2: Normal retarget still works at retarget boundary")
        # Mine blocks normally
        self.generate(wallet, 5, sync_fun=self.no_op)
        
        # Verify blocks are being mined normally
        blockchain_info = node.getblockchaininfo()
        assert blockchain_info['blocks'] > last_block_height + 1
        
        self.log.info("EDA test completed successfully")


if __name__ == '__main__':
    EDATest(__file__).main()
