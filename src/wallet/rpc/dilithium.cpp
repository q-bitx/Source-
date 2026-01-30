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
                "This is a legacy alias for getnewaddress with address_type=\"pq\".\n"
                "In Q-BitX, getnewaddress returns PQ addresses by default.\n"
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
    // Thin wrapper: same logic as getnewaddress but with address_type="pq" (DILITHIUM_LEGACY)
    std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) return UniValue::VNULL;

    LOCK(pwallet->cs_wallet);

    if (!pwallet->CanGetAddresses()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: This wallet has no available keys");
    }

    // Parse the label first so we don't generate a key if there's an error
    const std::string label{LabelFromValue(request.params[0])};

    // Use DILITHIUM_LEGACY output type (same as "pq")
    auto op_dest = pwallet->GetNewDestination(OutputType::DILITHIUM_LEGACY, label);
    if (!op_dest) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, util::ErrorString(op_dest).original);
    }

    return EncodeDestination(*op_dest);
},
    };
}

} // namespace wallet
