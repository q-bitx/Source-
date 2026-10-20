// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/miner.h>

#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <common/args.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <deploymentstatus.h>
#include <logging.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <pow.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <util/moneystr.h>
#include <util/time.h>
#include <validation.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace node {
namespace {
CTxOut WitnessCommitmentShape()
{
    CTxOut out;
    out.nValue = 0;
    out.scriptPubKey.resize(MINIMUM_WITNESS_COMMITMENT);
    out.scriptPubKey[0] = OP_RETURN;
    out.scriptPubKey[1] = 0x24;
    out.scriptPubKey[2] = 0xaa;
    out.scriptPubKey[3] = 0x21;
    out.scriptPubKey[4] = 0xa9;
    out.scriptPubKey[5] = 0xed;
    return out;
}

void AssertTransactionsPresent(const CBlock& block)
{
    for (const CTransactionRef& tx : block.vtx) {
        assert(tx);
    }
}

/** PQ P2PKH is the only consensus-valid coinbase vout[0] after genesis. */
CScript CanonicalPqPayoutScript()
{
    const std::vector<unsigned char> keyhash(20, 0);
    return CScript() << OP_DUP << OP_HASH160 << keyhash << OP_EQUALVERIFY << OP_CHECKSIGDILITHIUM;
}

CScript CoinbaseScriptSigPadded(int height, size_t target_size)
{
    CScript sig = CScript() << static_cast<int64_t>(height);
    // target_size is MAX_COINBASE_SCRIPTSIG_SIZE (100). That is below the
    // CompactSize step at 253, so padding does not widen the length prefix.
    assert(target_size < 253);
    assert(sig.size() <= target_size);
    sig.resize(target_size);
    return sig;
}
} // namespace

int64_t GetMinimumTime(const CBlockIndex* pindexPrev, const int64_t difficulty_adjustment_interval)
{
    int64_t min_time{pindexPrev->GetMedianTimePast() + 1};
    // Height of block to be mined.
    const int height{pindexPrev->nHeight + 1};
    // Account for BIP94 timewarp rule on all networks. This makes future
    // activation safer.
    if (height % difficulty_adjustment_interval == 0) {
        min_time = std::max<int64_t>(min_time, pindexPrev->GetBlockTime() - MAX_TIMEWARP);
    }
    return min_time;
}

int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime{std::max<int64_t>(GetMinimumTime(pindexPrev, consensusParams.DifficultyAdjustmentInterval()),
                                       TicksSinceEpoch<std::chrono::seconds>(NodeClock::now()))};

    if (nOldTime < nNewTime) {
        pblock->nTime = nNewTime;
    }

    // Updating time can change work required on testnet:
    if (consensusParams.fPowAllowMinDifficultyBlocks) {
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, consensusParams);
    }

    return nNewTime - nOldTime;
}

void RegenerateCommitments(CBlock& block, ChainstateManager& chainman)
{
    CMutableTransaction tx{*block.vtx.at(0)};
    tx.vout.erase(tx.vout.begin() + GetWitnessCommitmentIndex(block));
    block.vtx.at(0) = MakeTransactionRef(tx);

    const CBlockIndex* prev_block = WITH_LOCK(::cs_main, return chainman.m_blockman.LookupBlockIndex(block.hashPrevBlock));
    chainman.GenerateCoinbaseCommitment(block, prev_block);

    block.hashMerkleRoot = BlockMerkleRoot(block);
}

static BlockAssembler::Options ClampOptions(BlockAssembler::Options options)
{
    Assert(options.block_reserved_weight <= LEGACY_MAX_BLOCK_WEIGHT);
    Assert(options.block_reserved_weight >= MINIMUM_BLOCK_RESERVED_WEIGHT);
    Assert(options.coinbase_output_max_additional_sigops <= MAX_BLOCK_SIGOPS_COST);
    // Upper bound at legacy maximum; CreateNewBlock/TestPackage apply height-aware caps.
    options.nBlockMaxWeight = std::clamp<size_t>(options.nBlockMaxWeight, options.block_reserved_weight, LEGACY_MAX_BLOCK_WEIGHT);
    return options;
}

BlockAssembler::BlockAssembler(Chainstate& chainstate, const CTxMemPool* mempool, const Options& options)
    : chainparams{chainstate.m_chainman.GetParams()},
      m_mempool{options.use_mempool ? mempool : nullptr},
      m_chainstate{chainstate},
      m_options{ClampOptions(options)}
{
}

void ApplyArgsManOptions(const ArgsManager& args, BlockAssembler::Options& options)
{
    // Block resource limits
    options.nBlockMaxWeight = args.GetIntArg("-blockmaxweight", options.nBlockMaxWeight);
    if (const auto blockmintxfee{args.GetArg("-blockmintxfee")}) {
        if (const auto parsed{ParseMoney(*blockmintxfee)}) options.blockMinFeeRate = CFeeRate{*parsed};
    }
    options.print_modified_fee = args.GetBoolArg("-printpriority", options.print_modified_fee);
    options.block_reserved_weight = args.GetIntArg("-blockreservedweight", options.block_reserved_weight);
}

void BlockAssembler::resetBlock()
{
    inBlock.clear();

    // Reserve space for fixed-size block header, txs count, and coinbase tx.
    nBlockWeight = m_options.block_reserved_weight;
    nBlockSigOpsCost = m_options.coinbase_output_max_additional_sigops;

    // These counters do not include coinbase tx
    nBlockTx = 0;
    nBlockSerializedTxBytes = 0;
    m_coinbase_serialized_reserve = 0;
    nFees = 0;
}

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock()
{
    const auto time_start{SteadyClock::now()};

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());
    CBlock* const pblock = &pblocktemplate->block; // pointer for convenience

    LOCK(::cs_main);
    CBlockIndex* pindexPrev = m_chainstate.m_chain.Tip();
    assert(pindexPrev != nullptr);
    nHeight = pindexPrev->nHeight + 1;

    pblock->nVersion = m_chainstate.m_chainman.m_versionbitscache.ComputeBlockVersion(pindexPrev, chainparams.GetConsensus());
    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.MineBlocksOnDemand()) {
        pblock->nVersion = gArgs.GetIntArg("-blockversion", pblock->nVersion);
    }

    pblock->nTime = TicksSinceEpoch<std::chrono::seconds>(NodeClock::now());
    m_lock_time_cutoff = pindexPrev->GetMedianTimePast();

    // Real coinbase placeholder. Package selection must never serialize a null CTransactionRef.
    InstallCoinbasePlaceholder(pindexPrev);

    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    if (m_mempool) {
        addPackageTxs(nPackagesSelected, nDescendantsUpdated);
    }

    const auto time_1{SteadyClock::now()};

    m_last_block_num_txs = nBlockTx;
    m_last_block_weight = nBlockWeight;

    // Create coinbase transaction.
    CMutableTransaction coinbaseTx;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    coinbaseTx.vout.resize(1);
    coinbaseTx.vout[0].scriptPubKey = m_options.coinbase_output_script;
    coinbaseTx.vout[0].nValue = nFees + GetBlockSubsidy(nHeight, chainparams.GetConsensus());
//  coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0;
    coinbaseTx.vin[0].scriptSig = CScript() << static_cast<int64_t>(nHeight) << OP_0;
    pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));
    pblocktemplate->vchCoinbaseCommitment = m_chainstate.m_chainman.GenerateCoinbaseCommitment(*pblock, pindexPrev);

    const int tmpl_wscale{GetWitnessDiscountScale(chainparams.GetConsensus(), nHeight)};
    AssertTransactionsPresent(*pblock);
    const size_t block_serialized_size{::GetSerializeSize(TX_WITH_WITNESS(*pblock))};
    const uint64_t accounted_serialized{
        80 + static_cast<uint64_t>(GetSizeOfCompactSize(pblock->vtx.size())) +
        m_coinbase_serialized_reserve + nBlockSerializedTxBytes};
    // m_coinbase_serialized_reserve is the documented pool-coinbase upper bound,
    // so it may exceed the coinbase this node wrote. It must not fall short of
    // that coinbase's witness serialization.
    if (block_serialized_size > accounted_serialized) {
        throw std::runtime_error(strprintf("%s: serialized size %u exceeds accounted bytes %u",
                                            __func__, block_serialized_size, accounted_serialized));
    }
    LogPrintf("CreateNewBlock(): block serialized_size=%u bytes (max=%u), weight=%u txs=%u fees=%ld sigops=%d\n",
              block_serialized_size,
              Consensus::GetMaxBlockSerializedSize(chainparams.GetConsensus(), nHeight),
              static_cast<unsigned>(GetBlockWeightWithScale(*pblock, tmpl_wscale)),
              nBlockTx, nFees, nBlockSigOpsCost);

    // Fill in header
    pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
    UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
    pblock->nBits          = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());
    pblock->nNonce         = 0;

    BlockValidationState state;
    if (m_options.test_block_validity && !TestBlockValidity(state, chainparams, m_chainstate, *pblock, pindexPrev,
                                                            /*fCheckPOW=*/false, /*fCheckMerkleRoot=*/false)) {
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, state.ToString()));
    }
    const auto time_2{SteadyClock::now()};

    LogDebug(BCLog::BENCH, "CreateNewBlock() packages: %.2fms (%d packages, %d updated descendants), validity: %.2fms (total %.2fms)\n",
             Ticks<MillisecondsDouble>(time_1 - time_start), nPackagesSelected, nDescendantsUpdated,
             Ticks<MillisecondsDouble>(time_2 - time_1),
             Ticks<MillisecondsDouble>(time_2 - time_start));

    return std::move(pblocktemplate);
}

void BlockAssembler::onlyUnconfirmed(CTxMemPool::setEntries& testSet)
{
    for (CTxMemPool::setEntries::iterator iit = testSet.begin(); iit != testSet.end(); ) {
        // Only test txs not already in the block
        if (inBlock.count((*iit)->GetSharedTx()->GetHash())) {
            testSet.erase(iit++);
        } else {
            iit++;
        }
    }
}

bool BlockAssembler::TestPackage(uint64_t packageSize, int64_t packageSigOpsCost) const
{
    // TODO: switch to weight-based accounting for packages instead of vsize-based accounting.
    const int pkg_wscale{GetWitnessDiscountScale(chainparams.GetConsensus(), nHeight)};
    const size_t weight_cap{std::min(
        m_options.nBlockMaxWeight,
        static_cast<size_t>(Consensus::GetMaxBlockWeight(chainparams.GetConsensus(), nHeight)))};
    if (nBlockWeight + pkg_wscale * packageSize >= weight_cap) {
        return false;
    }
    if (nBlockSigOpsCost + packageSigOpsCost >= MAX_BLOCK_SIGOPS_COST) {
        return false;
    }
    // Physical serialized size is checked separately in TestAddedSerializedSize
    // once the ancestor transactions are known. packageSize is vsize and is not
    // a byte count, particularly after the PQ witness discount changes.
    return true;
}

bool BlockAssembler::TestAddedSerializedSize(uint64_t extra_bytes, uint64_t extra_txs) const
{
    if (!Consensus::EnforcesBlockSerializedSizeLimit(chainparams.GetConsensus(), nHeight)) {
        return true;
    }
    const uint64_t tx_count{1 + nBlockTx + extra_txs};
    const uint64_t projected{
        80 + static_cast<uint64_t>(GetSizeOfCompactSize(tx_count)) +
        m_coinbase_serialized_reserve + nBlockSerializedTxBytes + extra_bytes};
    const uint64_t max_serialized{Consensus::GetMaxBlockSerializedSize(chainparams.GetConsensus(), nHeight)};
    return projected <= max_serialized;
}

void BlockAssembler::InstallCoinbasePlaceholder(const CBlockIndex* pindexPrev)
{
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << static_cast<int64_t>(nHeight) << OP_0;
    coinbase.vout.resize(1);
    coinbase.vout[0].scriptPubKey = m_options.coinbase_output_script;
    coinbase.vout[0].nValue = 0;

    // Reserve the largest coinbase a pool can submit inside the GBT contract,
    // not the short BIP34 script this node stores in vtx[0].
    // scriptSig is padded to the consensus maximum (100). vout[0] is at least
    // a canonical PQ P2PKH, which is the only script CheckBlock accepts there.
    // The commitment output is always appended by GenerateCoinbaseCommitment,
    // including on testnet where the byte cap starts at 220000 and segwit at
    // 834624. The 32-byte nonce is included only when segwit is active, matching
    // UpdateUncommittedBlockStructures. GBT_POOL_COINBASE_EXTRA_OUTPUT_BYTES
    // then covers additional pool outputs. nValue is a fixed 8-byte field, so
    // subsidy versus zero does not change the size.
    CMutableTransaction reserved;
    reserved.vin.resize(1);
    reserved.vin[0].prevout.SetNull();
    reserved.vin[0].scriptSig = CoinbaseScriptSigPadded(nHeight, MAX_COINBASE_SCRIPTSIG_SIZE);
    const CScript& configured_payout{m_options.coinbase_output_script};
    const CScript canonical_payout{CanonicalPqPayoutScript()};
    reserved.vout.resize(1);
    reserved.vout[0].scriptPubKey = configured_payout.size() >= canonical_payout.size() ? configured_payout : canonical_payout;
    reserved.vout[0].nValue = 0;
    reserved.vout.push_back(WitnessCommitmentShape());
    const bool segwit_active{DeploymentActiveAfter(pindexPrev, m_chainstate.m_chainman, Consensus::DEPLOYMENT_SEGWIT)};
    if (segwit_active) {
        reserved.vin[0].scriptWitness.stack.assign(1, std::vector<unsigned char>(32, 0x00));
    }
    const CTransactionRef reserved_tx{MakeTransactionRef(std::move(reserved))};
    m_coinbase_serialized_reserve = reserved_tx->GetTotalSize() + GBT_POOL_COINBASE_EXTRA_OUTPUT_BYTES;

    // Weight accounting must leave the same coinbase room. Extra outputs are
    // non-witness, so they weigh (bytes * k). Header and tx-count CompactSize
    // are outside the coinbase. A 5-byte count covers every block that can
    // fit under the 8,000,000-byte cap (the 9-byte CompactSize starts only
    // above 2^32 transactions).
    const int scale{GetWitnessDiscountScale(chainparams.GetConsensus(), nHeight)};
    const int64_t reserved_weight{
        GetTransactionWeightWithScale(*reserved_tx, scale) +
        static_cast<int64_t>(GBT_POOL_COINBASE_EXTRA_OUTPUT_BYTES) * scale};
    const int64_t overhead_weight{static_cast<int64_t>(80 + 5) * scale};
    const uint64_t needed{static_cast<uint64_t>(overhead_weight + reserved_weight)};
    const uint64_t weight_cap{std::min(
        m_options.nBlockMaxWeight,
        static_cast<uint64_t>(Consensus::GetMaxBlockWeight(chainparams.GetConsensus(), nHeight)))};
    if (needed > nBlockWeight) {
        nBlockWeight = std::min(needed, weight_cap);
    }

    pblocktemplate->block.vtx.assign(1, MakeTransactionRef(std::move(coinbase)));
}

// Perform transaction-level checks before adding to block:
// - transaction finality (locktime)
bool BlockAssembler::TestPackageTransactions(const CTxMemPool::setEntries& package) const
{
    for (CTxMemPool::txiter it : package) {
        if (!IsFinalTx(it->GetTx(), nHeight, m_lock_time_cutoff)) {
            return false;
        }
    }
    return true;
}

void BlockAssembler::AddToBlock(CTxMemPool::txiter iter)
{
    pblocktemplate->block.vtx.emplace_back(iter->GetSharedTx());
    nBlockSerializedTxBytes += iter->GetTx().GetTotalSize();
    pblocktemplate->vTxFees.push_back(iter->GetFee());
    pblocktemplate->vTxSigOpsCost.push_back(iter->GetSigOpCost());
    nBlockWeight += iter->GetTxWeight();
    ++nBlockTx;
    nBlockSigOpsCost += iter->GetSigOpCost();
    nFees += iter->GetFee();
    inBlock.insert(iter->GetSharedTx()->GetHash());

    if (m_options.print_modified_fee) {
        LogPrintf("fee rate %s txid %s\n",
                  CFeeRate(iter->GetModifiedFee(), iter->GetTxSize()).ToString(),
                  iter->GetTx().GetHash().ToString());
    }
}

/** Add descendants of given transactions to mapModifiedTx with ancestor
 * state updated assuming given transactions are inBlock. Returns number
 * of updated descendants. */
static int UpdatePackagesForAdded(const CTxMemPool& mempool,
                                  const CTxMemPool::setEntries& alreadyAdded,
                                  indexed_modified_transaction_set& mapModifiedTx) EXCLUSIVE_LOCKS_REQUIRED(mempool.cs)
{
    AssertLockHeld(mempool.cs);

    int nDescendantsUpdated = 0;
    for (CTxMemPool::txiter it : alreadyAdded) {
        CTxMemPool::setEntries descendants;
        mempool.CalculateDescendants(it, descendants);
        // Insert all descendants (not yet in block) into the modified set
        for (CTxMemPool::txiter desc : descendants) {
            if (alreadyAdded.count(desc)) {
                continue;
            }
            ++nDescendantsUpdated;
            modtxiter mit = mapModifiedTx.find(desc);
            if (mit == mapModifiedTx.end()) {
                CTxMemPoolModifiedEntry modEntry(desc);
                mit = mapModifiedTx.insert(modEntry).first;
            }
            mapModifiedTx.modify(mit, update_for_parent_inclusion(it));
        }
    }
    return nDescendantsUpdated;
}

void BlockAssembler::SortForBlock(const CTxMemPool::setEntries& package, std::vector<CTxMemPool::txiter>& sortedEntries)
{
    // Sort package by ancestor count
    // If a transaction A depends on transaction B, then A's ancestor count
    // must be greater than B's.  So this is sufficient to validly order the
    // transactions for block inclusion.
    sortedEntries.clear();
    sortedEntries.insert(sortedEntries.begin(), package.begin(), package.end());
    std::sort(sortedEntries.begin(), sortedEntries.end(), CompareTxIterByAncestorCount());
}

// This transaction selection algorithm orders the mempool based
// on feerate of a transaction including all unconfirmed ancestors.
// Since we don't remove transactions from the mempool as we select them
// for block inclusion, we need an alternate method of updating the feerate
// of a transaction with its not-yet-selected ancestors as we go.
// This is accomplished by walking the in-mempool descendants of selected
// transactions and storing a temporary modified state in mapModifiedTxs.
// Each time through the loop, we compare the best transaction in
// mapModifiedTxs with the next transaction in the mempool to decide what
// transaction package to work on next.
void BlockAssembler::addPackageTxs(int& nPackagesSelected, int& nDescendantsUpdated)
{
    const auto& mempool{*Assert(m_mempool)};
    LOCK(mempool.cs);

    // mapModifiedTx will store sorted packages after they are modified
    // because some of their txs are already in the block
    indexed_modified_transaction_set mapModifiedTx;
    // Keep track of entries that failed inclusion, to avoid duplicate work
    std::set<Txid> failedTx;

    CTxMemPool::indexed_transaction_set::index<ancestor_score>::type::iterator mi = mempool.mapTx.get<ancestor_score>().begin();
    CTxMemPool::txiter iter;

    // Limit the number of attempts to add transactions to the block when it is
    // close to full; this is just a simple heuristic to finish quickly if the
    // mempool has a lot of entries.
    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    int64_t nConsecutiveFailed = 0;

    while (mi != mempool.mapTx.get<ancestor_score>().end() || !mapModifiedTx.empty()) {
        // First try to find a new transaction in mapTx to evaluate.
        //
        // Skip entries in mapTx that are already in a block or are present
        // in mapModifiedTx (which implies that the mapTx ancestor state is
        // stale due to ancestor inclusion in the block)
        // Also skip transactions that we've already failed to add. This can happen if
        // we consider a transaction in mapModifiedTx and it fails: we can then
        // potentially consider it again while walking mapTx.  It's currently
        // guaranteed to fail again, but as a belt-and-suspenders check we put it in
        // failedTx and avoid re-evaluation, since the re-evaluation would be using
        // cached size/sigops/fee values that are not actually correct.
        /** Return true if given transaction from mapTx has already been evaluated,
         * or if the transaction's cached data in mapTx is incorrect. */
        if (mi != mempool.mapTx.get<ancestor_score>().end()) {
            auto it = mempool.mapTx.project<0>(mi);
            assert(it != mempool.mapTx.end());
            if (mapModifiedTx.count(it) || inBlock.count(it->GetSharedTx()->GetHash()) || failedTx.count(it->GetSharedTx()->GetHash())) {
                ++mi;
                continue;
            }
        }

        // Now that mi is not stale, determine which transaction to evaluate:
        // the next entry from mapTx, or the best from mapModifiedTx?
        bool fUsingModified = false;

        modtxscoreiter modit = mapModifiedTx.get<ancestor_score>().begin();
        if (mi == mempool.mapTx.get<ancestor_score>().end()) {
            // We're out of entries in mapTx; use the entry from mapModifiedTx
            iter = modit->iter;
            fUsingModified = true;
        } else {
            // Try to compare the mapTx entry to the mapModifiedTx entry
            iter = mempool.mapTx.project<0>(mi);
            if (modit != mapModifiedTx.get<ancestor_score>().end() &&
                    CompareTxMemPoolEntryByAncestorFee()(*modit, CTxMemPoolModifiedEntry(iter))) {
                // The best entry in mapModifiedTx has higher score
                // than the one from mapTx.
                // Switch which transaction (package) to consider
                iter = modit->iter;
                fUsingModified = true;
            } else {
                // Either no entry in mapModifiedTx, or it's worse than mapTx.
                // Increment mi for the next loop iteration.
                ++mi;
            }
        }

        // We skip mapTx entries that are inBlock, and mapModifiedTx shouldn't
        // contain anything that is inBlock.
        assert(!inBlock.count(iter->GetSharedTx()->GetHash()));

        uint64_t packageSize = iter->GetSizeWithAncestors();
        CAmount packageFees = iter->GetModFeesWithAncestors();
        int64_t packageSigOpsCost = iter->GetSigOpCostWithAncestors();
        if (fUsingModified) {
            packageSize = modit->nSizeWithAncestors;
            packageFees = modit->nModFeesWithAncestors;
            packageSigOpsCost = modit->nSigOpCostWithAncestors;
        }

        if (packageFees < m_options.blockMinFeeRate.GetFee(packageSize)) {
            // Everything else we might consider has a lower fee rate
            return;
        }

        if (!TestPackage(packageSize, packageSigOpsCost)) {
            if (fUsingModified) {
                // Since we always look at the best entry in mapModifiedTx,
                // we must erase failed entries so that we can consider the
                // next best entry on the next loop iteration
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter->GetSharedTx()->GetHash());
            }

            ++nConsecutiveFailed;

            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockWeight >
                    std::min(m_options.nBlockMaxWeight,
                             static_cast<size_t>(Consensus::GetMaxBlockWeight(chainparams.GetConsensus(), nHeight)))
                    - m_options.block_reserved_weight) {
                // Give up if we're close to full and haven't succeeded in a while
                break;
            }
            continue;
        }

        auto ancestors{mempool.AssumeCalculateMemPoolAncestors(__func__, *iter, CTxMemPool::Limits::NoLimits(), /*fSearchForParents=*/false)};

        onlyUnconfirmed(ancestors);
        ancestors.insert(iter);

        // Test if all tx's are Final
        if (!TestPackageTransactions(ancestors)) {
            if (fUsingModified) {
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter->GetSharedTx()->GetHash());
            }
            continue;
        }

        uint64_t package_bytes{0};
        for (CTxMemPool::txiter it : ancestors) {
            package_bytes += it->GetTx().GetTotalSize();
        }
        if (!TestAddedSerializedSize(package_bytes, ancestors.size())) {
            if (fUsingModified) {
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter->GetSharedTx()->GetHash());
            }
            ++nConsecutiveFailed;
            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockWeight >
                    std::min(m_options.nBlockMaxWeight,
                             static_cast<size_t>(Consensus::GetMaxBlockWeight(chainparams.GetConsensus(), nHeight)))
                    - m_options.block_reserved_weight) {
                break;
            }
            continue;
        }

        // This transaction will make it in; reset the failed counter.
        nConsecutiveFailed = 0;

        // Package can be added. Sort the entries in a valid order.
        std::vector<CTxMemPool::txiter> sortedEntries;
        SortForBlock(ancestors, sortedEntries);

        for (size_t i = 0; i < sortedEntries.size(); ++i) {
            AddToBlock(sortedEntries[i]);
            // Erase from the modified set, if present
            mapModifiedTx.erase(sortedEntries[i]);
        }

        ++nPackagesSelected;
        pblocktemplate->m_package_feerates.emplace_back(packageFees, static_cast<int32_t>(packageSize));

        // Update transactions that depend on each of these
        nDescendantsUpdated += UpdatePackagesForAdded(mempool, ancestors, mapModifiedTx);
    }
}
} // namespace node
