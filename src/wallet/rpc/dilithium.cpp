// Copyright (c) 2009-present The Bitcoin Core developers
// Copyright (c) Q-BitX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <key_io.h>
#include <outputtype.h>
#include <rpc/util.h>
#include <util/translation.h>
#include <wallet/rpc/util.h>
#include <wallet/wallet.h>

#include <univalue.h>

namespace wallet {

RPCHelpMan getnewdilithiumaddress()
{
    return RPCHelpMan{"getnewdilithiumaddress",
                "\n[LEGACY ALIAS] Returns a new Dilithium (PQ) address for receiving payments.\n"
                "Same as getnewaddress with address_type=\"pq\" (auto PQ: legacy before PQ witness activation, native witness after).\n"
                "If 'label' is specified, it is added to the address book \n"
                "so payments received with the address will be associated with 'label'.\n",
                {
                    {"label", RPCArg::Type::STR, RPCArg::Default{""}, "The label name for the address to be linked to. It can also be set to the empty string \"\" to represent the default label. The label does not need to exist, it will be created if there is no label by the given name."},
                },
                RPCResult{
                    RPCResult::Type::STR, "address", "The new Dilithium address"
                },
                RPCExamples{
                    "\nGet a new Dilithium address (same as getnewaddress)\n"
                    + HelpExampleCli("getnewdilithiumaddress", "") +
                    "\nGet a new Dilithium address with a label\n"
                    + HelpExampleCli("getnewdilithiumaddress", "\"mylabel\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    if (!pwallet->CanGetAddresses()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: This wallet has no available keys");
    }

    const std::string label{LabelFromValue(request.params[0])};

    const bool pq_witness_next = IsPqWitnessActiveForNextBlock(pwallet->chain());
    const OutputType output_type = pq_witness_next ? OutputType::DILITHIUM_BECH32 : OutputType::DILITHIUM_LEGACY;

    auto op_dest = pwallet->GetNewDestination(output_type, label);
    if (!op_dest) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, util::ErrorString(op_dest).original);
    }

    return EncodeDestination(*op_dest);
},
    };
}

RPCHelpMan getnewlegacypqaddress()
{
    return RPCHelpMan{"getnewlegacypqaddress",
                "\n[REGTEST / DEV / MANUAL TESTING ONLY] Returns a new wallet-owned legacy-format Dilithium (PQ) address\n"
                "(base58 P2PKH-style PQ, e.g. prefix 27...), even after PQ witness activation when getnewaddress/getnewdilithiumaddress\n"
                "return native witness (rdil1...). Keys are stored the same way as getnewaddress with explicit dilithium-legacy.\n"
                "Use for tests such as generatetoaddress coinbase to legacy PQ and pqsendtoaddress from legacy to witness.\n"
                "Do not rely on this RPC for production workflows.\n",
                {
                    {"label", RPCArg::Type::STR, RPCArg::Default{""}, "Optional label for the address book (same semantics as getnewaddress)."},
                },
                RPCResult{
                    RPCResult::Type::STR, "address", "The new legacy-format Dilithium (PQ) address"
                },
                RPCExamples{
                    "\nRegtest: force a legacy PQ receive address after PQ witness is active\n"
                    + HelpExampleCli("getnewlegacypqaddress", "") +
                    "\nWith label\n"
                    + HelpExampleCli("getnewlegacypqaddress", "\"mylabel\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    if (!pwallet->CanGetAddresses()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: This wallet has no available keys");
    }

    const std::string label{request.params.empty() ? "" : LabelFromValue(request.params[0])};

    auto op_dest = pwallet->GetNewDestination(OutputType::DILITHIUM_LEGACY, label);
    if (!op_dest) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, util::ErrorString(op_dest).original);
    }
    if (!std::holds_alternative<DilithiumPKHash>(*op_dest)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Legacy PQ address generation did not produce a DilithiumPKHash destination");
    }

    return EncodeDestination(*op_dest);
},
    };
}

} // namespace wallet
