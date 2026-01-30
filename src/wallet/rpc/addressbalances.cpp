// Copyright (c) 2025-present The Q-BitX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <addresstype.h>
#include <core_io.h>
#include <consensus/consensus.h>
#include <key_io.h>
#include <rpc/util.h>
#include <script/solver.h>
#include <util/moneystr.h>
#include <wallet/coincontrol.h>
#include <wallet/rpc/util.h>
#include <wallet/spend.h>
#include <wallet/wallet.h>

#include <univalue.h>

namespace wallet {

RPCHelpMan getaddressbalances()
{
    return RPCHelpMan{"getaddressbalances",
        "\nReturns balances for all wallet-owned addresses (including PQ/Dilithium addresses), grouped by address.\n"
        "This is the primary command to view all your addresses and their balances in one place.\n"
        "Shows confirmed, unconfirmed, and immature balances per address.\n",
        {
            {"minconf", RPCArg::Type::NUM, RPCArg::Default{1}, "The minimum number of confirmations before funds are included in the balance"},
            {"include_unsafe", RPCArg::Type::BOOL, RPCArg::Default{true}, "Include outputs that are not safe to spend\n"
                      "See description in listunspent."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "wallet", "The wallet name"},
                {RPCResult::Type::ARR, "by_address", "Balances grouped by address",
                {
                    {RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::STR, "address", "The address (PQ/Dilithium or Bitcoin-style)"},
                        {RPCResult::Type::STR, "label", "The address label (empty string if no label)"},
                        {RPCResult::Type::STR_AMOUNT, "confirmed", "Confirmed balance (minconf or more confirmations)"},
                        {RPCResult::Type::STR_AMOUNT, "unconfirmed", "Unconfirmed balance (less than minconf confirmations)"},
                        {RPCResult::Type::STR_AMOUNT, "immature", "Immature balance (coinbase not yet matured)"},
                        {RPCResult::Type::STR_AMOUNT, "total", "Total balance (confirmed + unconfirmed + immature)"},
                        {RPCResult::Type::NUM, "utxos", "Number of UTXOs for this address"},
                    }},
                }},
                {RPCResult::Type::OBJ, "totals", "Total balances across all addresses",
                {
                    {RPCResult::Type::STR_AMOUNT, "confirmed", "Total confirmed balance"},
                    {RPCResult::Type::STR_AMOUNT, "unconfirmed", "Total unconfirmed balance"},
                    {RPCResult::Type::STR_AMOUNT, "immature", "Total immature balance"},
                    {RPCResult::Type::STR_AMOUNT, "total", "Total balance"},
                }},
            }
        },
        RPCExamples{
            HelpExampleCli("getaddressbalances", "") +
            HelpExampleCli("getaddressbalances", "0") +
            HelpExampleRpc("getaddressbalances", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            const std::shared_ptr<const CWallet> pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;

            int minconf = 1;
            if (!request.params[0].isNull()) {
                minconf = request.params[0].getInt<int>();
                if (minconf < 0) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "minconf cannot be negative");
                }
            }

            bool include_unsafe = true;
            if (!request.params[1].isNull()) {
                include_unsafe = request.params[1].get_bool();
            }

            // Make sure the results are valid at least up to the most recent block
            pwallet->BlockUntilSyncedToCurrentChain();

            LOCK(pwallet->cs_wallet);

            // Collect coins using AvailableCoins
            CCoinControl coin_control;
            coin_control.m_include_unsafe_inputs = include_unsafe;
            CoinFilterParams coins_params;
            coins_params.only_spendable = false;
            coins_params.skip_locked = false;
            coins_params.include_immature_coinbase = true; // Include immature to show in immature bucket

            std::vector<COutput> vecOutputs;
            {
                // AvailableCoins requires lock
                vecOutputs = AvailableCoins(*pwallet, &coin_control, /*feerate=*/std::nullopt, coins_params).All();
            }

            // Structure to hold balances per address
            struct AddressBalance {
                std::string address;
                std::string label;
                CAmount confirmed{0};
                CAmount unconfirmed{0};
                CAmount immature{0};
                size_t utxos{0};
            };
            std::map<std::string, AddressBalance> address_map;

            // Process each output
            for (const COutput& out : vecOutputs) {
                CTxDestination dest;
                const CScript& scriptPubKey = out.txout.scriptPubKey;
                if (!ExtractDestination(scriptPubKey, dest)) {
                    // Skip outputs without extractable destination
                    continue;
                }

                std::string address_str = EncodeDestination(dest);
                if (address_map.find(address_str) == address_map.end()) {
                    // Initialize new address entry
                    address_map[address_str].address = address_str;
                    const auto* address_book_entry = pwallet->FindAddressBookEntry(dest, /*allow_change=*/false);
                    if (address_book_entry) {
                        address_map[address_str].label = address_book_entry->GetLabel();
                    } else {
                        address_map[address_str].label = "";
                    }
                }

                AddressBalance& addr_bal = address_map[address_str];

                // Get wallet transaction to check coinbase and maturity
                const CWalletTx* wtx = pwallet->GetWalletTx(out.outpoint.hash);
                if (!wtx) {
                    // Should not happen, but skip if it does
                    continue;
                }

                bool is_immature = false;
                if (wtx->IsCoinBase()) {
                    if (pwallet->IsTxImmatureCoinBase(*wtx)) {
                        is_immature = true;
                    }
                }

                // Categorize balance
                if (is_immature) {
                    addr_bal.immature += out.txout.nValue;
                } else if (out.depth >= minconf) {
                    addr_bal.confirmed += out.txout.nValue;
                } else {
                    addr_bal.unconfirmed += out.txout.nValue;
                }
                addr_bal.utxos++;
            }

            // Build result
            UniValue by_address(UniValue::VARR);
            CAmount total_confirmed{0};
            CAmount total_unconfirmed{0};
            CAmount total_immature{0};

            for (const auto& [addr_str, addr_bal] : address_map) {
                UniValue entry(UniValue::VOBJ);
                entry.pushKV("address", addr_bal.address);
                entry.pushKV("label", addr_bal.label);
                entry.pushKV("confirmed", ValueFromAmount(addr_bal.confirmed));
                entry.pushKV("unconfirmed", ValueFromAmount(addr_bal.unconfirmed));
                entry.pushKV("immature", ValueFromAmount(addr_bal.immature));
                CAmount total = addr_bal.confirmed + addr_bal.unconfirmed + addr_bal.immature;
                entry.pushKV("total", ValueFromAmount(total));
                entry.pushKV("utxos", static_cast<uint64_t>(addr_bal.utxos));

                by_address.push_back(std::move(entry));

                total_confirmed += addr_bal.confirmed;
                total_unconfirmed += addr_bal.unconfirmed;
                total_immature += addr_bal.immature;
            }

            UniValue totals(UniValue::VOBJ);
            totals.pushKV("confirmed", ValueFromAmount(total_confirmed));
            totals.pushKV("unconfirmed", ValueFromAmount(total_unconfirmed));
            totals.pushKV("immature", ValueFromAmount(total_immature));
            CAmount total_total = total_confirmed + total_unconfirmed + total_immature;
            totals.pushKV("total", ValueFromAmount(total_total));

            UniValue result(UniValue::VOBJ);
            result.pushKV("wallet", pwallet->GetName());
            result.pushKV("by_address", std::move(by_address));
            result.pushKV("totals", std::move(totals));

            return result;
        },
    };
}

} // namespace wallet
