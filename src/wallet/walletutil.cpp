// Copyright (c) 2017-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/walletutil.h>

#include <chainparams.h>
#include <common/args.h>
#include <key_io.h>
#include <logging.h>
#include <outputtype.h>
#include <tinyformat.h>

namespace wallet {
fs::path GetWalletDir()
{
    fs::path path;

    if (gArgs.IsArgSet("-walletdir")) {
        path = gArgs.GetPathArg("-walletdir");
        if (!fs::is_directory(path)) {
            // If the path specified doesn't exist, we return the deliberately
            // invalid empty string.
            path = "";
        }
    } else {
        path = gArgs.GetDataDirNet();
        // If a wallets directory exists, use that, otherwise default to GetDataDir
        if (fs::is_directory(path / "wallets")) {
            path /= "wallets";
        }
    }

    return path;
}

bool IsFeatureSupported(int wallet_version, int feature_version)
{
    return wallet_version >= feature_version;
}

WalletFeature GetClosestWalletFeature(int version)
{
    static constexpr std::array wallet_features{FEATURE_LATEST, FEATURE_PRE_SPLIT_KEYPOOL, FEATURE_NO_DEFAULT_KEY, FEATURE_HD_SPLIT, FEATURE_HD, FEATURE_COMPRPUBKEY, FEATURE_WALLETCRYPT, FEATURE_BASE};
    for (const WalletFeature& wf : wallet_features) {
        if (version >= wf) return wf;
    }
    return static_cast<WalletFeature>(0);
}

WalletDescriptor GenerateWalletDescriptor(const CExtPubKey& master_key, const OutputType& addr_type, bool internal)
{
    // Reject unsupported output types (e.g., Dilithium types)
    if (!IsWalletSupportedOutputType(addr_type)) {
        throw std::runtime_error(strprintf("Output type %s is not supported by standard wallet descriptors. Dilithium types must be handled via PQ RPC.", FormatOutputType(addr_type)));
    }

    int64_t creation_time = GetTime();

    std::string xpub = EncodeExtPubKey(master_key);

    // Build descriptor string
    std::string desc_prefix;
    std::string desc_suffix = "/*)";
    switch (addr_type) {
    case OutputType::LEGACY: {
        desc_prefix = "pkh(" + xpub + "/44h";
        break;
    }
    case OutputType::P2SH_SEGWIT: {
        desc_prefix = "sh(wpkh(" + xpub + "/49h";
        desc_suffix += ")";
        break;
    }
    case OutputType::BECH32: {
        desc_prefix = "wpkh(" + xpub + "/84h";
        break;
    }
    case OutputType::BECH32M: {
        desc_prefix = "tr(" + xpub + "/86h";
        break;
    }
    case OutputType::UNKNOWN: {
        // We should never have a DescriptorScriptPubKeyMan for an UNKNOWN OutputType,
        // so if we get to this point something is wrong
        throw std::runtime_error("Cannot generate descriptor for UNKNOWN output type");
    }
    default: {
        // This should not be reachable due to IsWalletSupportedOutputType check above,
        // but handle it gracefully for safety
        throw std::runtime_error(strprintf("Unsupported output type %s in GenerateWalletDescriptor", FormatOutputType(addr_type)));
    }
    }
    
    if (desc_prefix.empty()) {
        throw std::runtime_error(strprintf("Failed to generate descriptor prefix for output type %s", FormatOutputType(addr_type)));
    }

    // Mainnet derives at 0', testnet and regtest derive at 1'
    if (Params().IsTestChain()) {
        desc_prefix += "/1h";
    } else {
        desc_prefix += "/0h";
    }

    std::string internal_path = internal ? "/1" : "/0";
    std::string desc_str = desc_prefix + "/0h" + internal_path + desc_suffix;

    // Make the descriptor
    FlatSigningProvider keys;
    std::string error;
    std::vector<std::unique_ptr<Descriptor>> desc = Parse(desc_str, keys, error, false);
    WalletDescriptor w_desc(std::move(desc.at(0)), creation_time, 0, 0, 0);
    return w_desc;
}

} // namespace wallet
