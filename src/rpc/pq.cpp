// Copyright (c) 2025-present The Q-BitX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <chainparams.h>
#include <coins.h>
#include <consensus/amount.h>
#include <core_io.h>
#include <crypto/dilithium.h>
#include <crypto/dilithium_key.h>
#include <primitives/transaction.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#ifdef ENABLE_WALLET
#include <interfaces/chain.h>
#include <wallet/rpc/util.h>
#include <wallet/wallet.h>
#include <script/signingprovider.h>
#include <wallet/scriptpubkeyman.h>
#include <wallet/walletutil.h>
#endif
#include <script/descriptor.h>
#include <script/interpreter.h>
#include <script/opcodes.h>
#include <script/script.h>
#include <script/solver.h>
#include <script/standard.h>
#include <sync.h>
#include <tinyformat.h>
#include <univalue.h>
#include <util/moneystr.h>
#include <util/strencodings.h>
#include <validation.h>
#include <addresstype.h>
#include <key_io.h>
#include <node/transaction.h>
#include <node/types.h>
#include <uint256.h>
#include <util/transaction_identifier.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#ifdef ENABLE_WALLET
using interfaces::FoundBlock;
#endif

using valtype = std::vector<unsigned char>;

static RPCHelpMan pqkeypair()
{
    return RPCHelpMan{"pqkeypair",
        "\nGenerate a Dilithium3 keypair (regtest only).\n",
        {},
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "pubkey", "Dilithium3 public key (hex)"},
                {RPCResult::Type::STR_HEX, "privkey", "Dilithium3 private key (hex)"},
            }
        },
        RPCExamples{
            HelpExampleCli("pqkeypair", "")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            if (!Params().IsMockableChain()) {
                throw std::runtime_error("pqkeypair is for regression testing (-regtest mode) only");
            }

            valtype pubkey, privkey;
            if (!PQ_GenerateKeypair(pubkey, privkey)) {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to generate Dilithium keypair");
            }
            if (pubkey.size() != DILITHIUM_PUBLICKEYBYTES) {
                throw JSONRPCError(RPC_INTERNAL_ERROR,
                    strprintf("Unexpected Dilithium public key size: %u bytes (expected %u)",
                        static_cast<unsigned int>(pubkey.size()),
                        static_cast<unsigned int>(DILITHIUM_PUBLICKEYBYTES)));
            }
            if (privkey.size() != DILITHIUM_SECRETKEYBYTES) {
                throw JSONRPCError(RPC_INTERNAL_ERROR,
                    strprintf("Unexpected Dilithium private key size: %u bytes (expected %u)",
                        static_cast<unsigned int>(privkey.size()),
                        static_cast<unsigned int>(DILITHIUM_SECRETKEYBYTES)));
            }

            UniValue result(UniValue::VOBJ);
            result.pushKV("pubkey", HexStr(pubkey));
            result.pushKV("privkey", HexStr(privkey));
            return result;
        },
    };
}

static RPCHelpMan pqsighash()
{
    return RPCHelpMan{"pqsighash",
        "\nCompute signature hash for a transaction input using SigVersion::BASE (regtest only).\n",
        {
            {"rawtx", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The raw transaction hex"},
            {"vin", RPCArg::Type::NUM, RPCArg::Optional::NO, "Input index (0-based)"},
            {"scriptcode", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The scriptCode hex"},
            {"hashtype", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Signature hash type (default: 1 = SIGHASH_ALL)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "sighash", "32-byte signature hash (hex)"},
            }
        },
        RPCExamples{
            HelpExampleCli("pqsighash", "\"01000000...\" 0 \"76a914...\" 1")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            if (!Params().IsMockableChain()) {
                throw std::runtime_error("pqsighash is for regression testing (-regtest mode) only");
            }

            // Parse raw transaction
            CMutableTransaction mtx;
            if (!DecodeHexTx(mtx, request.params[0].get_str(), /*try_no_witness=*/true, /*try_witness=*/true)) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
            }
            CTransaction tx(mtx);

            // Parse input index
            unsigned int nIn = request.params[1].getInt<unsigned int>();
            if (nIn >= tx.vin.size()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Input index out of range");
            }

            // Parse scriptCode
            std::vector<unsigned char> scriptCodeBytes = ParseHex(request.params[2].get_str());
            CScript scriptCode(scriptCodeBytes.begin(), scriptCodeBytes.end());

            // Parse hashtype (default SIGHASH_ALL = 1)
            int32_t nHashType = SIGHASH_ALL;
            if (request.params.size() > 3 && !request.params[3].isNull()) {
                nHashType = request.params[3].getInt<int32_t>();
            }

            // For BASE (pre-segwit), amount is not used, but we need to provide it
            // Use 0 as default for BASE mode
            CAmount amount = 0;

            // Compute signature hash using SigVersion::BASE
            uint256 sighash = SignatureHash(scriptCode, tx, nIn, nHashType, amount, SigVersion::BASE, nullptr);

            UniValue result(UniValue::VOBJ);
            result.pushKV("sighash", sighash.GetHex());
            return result;
        },
    };
}

static RPCHelpMan pqsign()
{
    return RPCHelpMan{"pqsign",
        "\nSign a 32-byte message hash with Dilithium3 (regtest only).\n",
        {
            {"messagehash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "32-byte message hash (hex)"},
            {"privkey", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Dilithium3 private key (hex)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "sig", "Dilithium3 signature (hex, without hashtype byte)"},
            }
        },
        RPCExamples{
            HelpExampleCli("pqsign", "\"abcd1234...\" \"privkeyhex...\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            if (!Params().IsMockableChain()) {
                throw std::runtime_error("pqsign is for regression testing (-regtest mode) only");
            }

            // Parse message hash
            std::vector<unsigned char> msgHash = ParseHex(request.params[0].get_str());
            if (msgHash.size() != 32) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Message hash must be exactly 32 bytes");
            }

            // Parse private key
            std::vector<unsigned char> privkey = ParseHex(request.params[1].get_str());
            if (privkey.size() != DILITHIUM_SECRETKEYBYTES) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Private key must be exactly %u bytes", DILITHIUM_SECRETKEYBYTES));
            }

            // Sign the message hash
            valtype signature;
            if (!PQ_Sign(signature, msgHash, privkey)) {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to sign message hash");
            }

            UniValue result(UniValue::VOBJ);
            result.pushKV("sig", HexStr(signature));
            return result;
        },
    };
}

static RPCHelpMan pqgetdescriptor()
{
    return RPCHelpMan{"pqgetdescriptor",
        "\nGenerate a descriptor for a PQ (Dilithium3) public key (regtest only).\n"
        "Returns a raw() descriptor that can be used with generatetodescriptor and scantxoutset.\n",
        {
            {"pubkey_hex", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Dilithium3 public key (hex, same encoding as pqkeypair's pubkey)"},
            {"type", RPCArg::Type::STR, RPCArg::Default{"p2dpkh"}, "Output type: \"p2dpkh\" (default) or \"witness_v0\""},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "type", "Output type: \"p2dpkh\" or \"witness_v0\""},
                {RPCResult::Type::STR_HEX, "pubkey_hex", "Normalized public key hex"},
                {RPCResult::Type::STR_HEX, "scriptPubKey", "The scriptPubKey in hex format"},
                {RPCResult::Type::STR, "descriptor", "Descriptor string with checksum (raw(...)#...)"},
                {RPCResult::Type::STR, "address", "Encoded address (if available)"},
            }
        },
        RPCExamples{
            "\nRegtest workflow: generate keypair, get descriptor, then generate blocks\n"
            + HelpExampleCli("pqkeypair", "") +
            "\nUse the pubkey from pqkeypair output with default p2dpkh type\n"
            + HelpExampleCli("pqgetdescriptor", "\"<pubkey_from_pqkeypair>\"") +
            "\nUse witness_v0 type\n"
            + HelpExampleCli("pqgetdescriptor", "\"<pubkey_from_pqkeypair>\" \"witness_v0\"") +
            "\nGenerate 101 blocks to the descriptor\n"
            + HelpExampleCli("generatetodescriptor", "101 \"<descriptor_from_pqgetdescriptor>\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            if (!Params().IsMockableChain()) {
                throw std::runtime_error("pqgetdescriptor is for regression testing (-regtest mode) only");
            }

            // Parse and validate pubkey hex
            std::string pubkeyHex = request.params[0].get_str();
            if (pubkeyHex.empty()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Public key cannot be empty");
            }

            // Validate hex format
            if (!IsHex(pubkeyHex)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Public key must be valid hex");
            }

            // Parse hex to bytes
            std::vector<unsigned char> pubkeyBytes = ParseHex(pubkeyHex);
            
            // Validate size - must be exactly CDilithiumPubKey::SIZE
            if (pubkeyBytes.size() != CDilithiumPubKey::SIZE) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, 
                    strprintf("Public key must be exactly %u bytes (got %u bytes)",
                        static_cast<unsigned int>(CDilithiumPubKey::SIZE),
                        static_cast<unsigned int>(pubkeyBytes.size())));
            }

            // Create CDilithiumPubKey
            CDilithiumPubKey dilithiumPubKey(pubkeyBytes);
            if (!dilithiumPubKey.IsValid()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid Dilithium public key");
            }

            // Parse type parameter (default: "p2dpkh")
            std::string typeStr = "p2dpkh";
            if (request.params.size() > 1 && !request.params[1].isNull()) {
                typeStr = request.params[1].get_str();
            }

            // Normalize type string to lowercase
            std::transform(typeStr.begin(), typeStr.end(), typeStr.begin(), ::tolower);

            // Create destination based on type
            CTxDestination dest;
            std::string outputType;
            if (typeStr == "p2dpkh") {
                dest = DilithiumPKHash(dilithiumPubKey);
                outputType = "p2dpkh";
            } else if (typeStr == "witness_v0") {
                dest = DilithiumWitnessV0KeyHash(dilithiumPubKey);
                outputType = "witness_v0";
            } else {
                throw JSONRPCError(RPC_INVALID_PARAMETER, 
                    "Invalid type. Must be \"p2dpkh\" (default) or \"witness_v0\"");
            }

            // Get scriptPubKey from destination
            CScript scriptPubKey = GetScriptForDestination(dest);

            // Check that resulting script stays under MAX_SCRIPT_SIZE
            if (scriptPubKey.size() > MAX_SCRIPT_SIZE) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, 
                    strprintf("Resulting scriptPubKey size (%u bytes) exceeds MAX_SCRIPT_SIZE (%u bytes)",
                        static_cast<unsigned int>(scriptPubKey.size()),
                        MAX_SCRIPT_SIZE));
            }

            // Convert script to hex
            std::string scriptHex = HexStr(scriptPubKey);

            // Build descriptor body: raw(<hex>)
            std::string descriptorBody = "raw(" + scriptHex + ")";

            // Get and append checksum
            std::string checksum = GetDescriptorChecksum(descriptorBody);
            if (checksum.empty()) {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to compute descriptor checksum");
            }
            std::string descriptor = descriptorBody + "#" + checksum;

            // Normalize pubkey hex (lowercase)
            std::string normalizedPubkeyHex = HexStr(pubkeyBytes);

            // Return object with all fields
            UniValue result(UniValue::VOBJ);
            result.pushKV("type", outputType);
            result.pushKV("pubkey_hex", normalizedPubkeyHex);
            result.pushKV("scriptPubKey", scriptHex);
            result.pushKV("descriptor", descriptor);

            // Optionally include address if EncodeDestination returns non-empty string
            std::string address = EncodeDestination(dest);
            if (!address.empty()) {
                result.pushKV("address", address);
            }

            return result;
        },
    };
}

static RPCHelpMan pqsetinputscript()
{
    return RPCHelpMan{"pqsetinputscript",
        "\nSet scriptSig for a given input of a raw transaction (returns new raw tx hex).\n"
        "Low-level helper for PQ spends (regtest/dev flows).\n",
        {
            {"rawtx", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The raw transaction hex"},
            {"vin", RPCArg::Type::NUM, RPCArg::Optional::NO, "Input index (0-based)"},
            {"scriptsig", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The scriptSig to set (hex)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "rawtx", "The updated raw transaction hex"},
            }
        },
        RPCExamples{
            HelpExampleCli("pqsetinputscript", "\"01000000...\" 0 \"<scriptsighex>\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            if (!Params().IsMockableChain()) {
                throw std::runtime_error("pqsetinputscript is for regression testing (-regtest mode) only");
            }

            // Parse raw transaction
            CMutableTransaction mtx;
            if (!DecodeHexTx(mtx, request.params[0].get_str(), /*try_no_witness=*/true, /*try_witness=*/true)) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
            }

            // Parse input index
            unsigned int nIn = request.params[1].getInt<unsigned int>();
            if (nIn >= mtx.vin.size()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Input index out of range");
            }

            // Parse scriptSig hex
            std::string scriptSigHex = request.params[2].get_str();
            if (!IsHex(scriptSigHex)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "scriptSig must be valid hex");
            }
            std::vector<unsigned char> scriptSigBytes = ParseHex(scriptSigHex);
            CScript scriptSig(scriptSigBytes.begin(), scriptSigBytes.end());

            // Set the scriptSig for the specified input
            mtx.vin[nIn].scriptSig = scriptSig;

            // Encode back to hex
            CTransaction tx(mtx);
            std::string rawtxHex = EncodeHexTx(tx);

            UniValue result(UniValue::VOBJ);
            result.pushKV("rawtx", rawtxHex);
            return result;
        },
    };
}

/** Native PQ witness keyhash: consensus-visible OP_2 <20-byte program> (22-byte script). */
static bool IsNativePQWitnessKeyhashScript(const CScript& script)
{
    return script.size() == 22 &&
           static_cast<unsigned char>(script[0]) == static_cast<unsigned char>(OP_2) &&
           static_cast<unsigned char>(script[1]) == 0x14;
}

struct PqSendtoSigned {
    CTransactionRef tx;
    std::string to_address;
    CAmount amount;
    CAmount fee;
    CAmount change;
    bool native_pq_witness;
    std::string scriptSig_hex;
    int64_t witness_items;
    int64_t witness0_size;
    int64_t witness1_size;
};

/** Parse vout for pqsendto / pqcreaterawspend (JSON number or string, e.g. from bitcoin-cli). */
static uint32_t ParsePqSendtoVout(const UniValue& v)
{
    if (v.isNum()) {
        const int64_t n = v.getInt<int64_t>();
        if (n < 0 || n > std::numeric_limits<uint32_t>::max()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "from_vout is out of range");
        }
        return static_cast<uint32_t>(n);
    }
    if (v.isStr()) {
        uint32_t out;
        if (!ParseUInt32(v.get_str(), &out)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "from_vout must be a decimal integer");
        }
        return out;
    }
    throw JSONRPCError(RPC_TYPE_ERROR, "from_vout must be numeric");
}

/** Build and sign the same single-input PQ send transaction as pqsendto (no broadcast). */
static PqSendtoSigned PqBuildSignedSendtoTx(const JSONRPCRequest& request)
{
    // Parse from_txid
    std::string txidHex = request.params[0].get_str();
    if (txidHex.length() != 64) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "from_txid must be 64 hex characters (32 bytes)");
    }
    auto txid_opt = Txid::FromHex(txidHex);
    if (!txid_opt) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "from_txid must be 64 hex characters (32 bytes)");
    }
    const Txid txid = *txid_opt;

    // Parse from_vout
    const uint32_t vout = ParsePqSendtoVout(request.params[1]);

    // Parse from_scriptcode_hex
    std::string scriptCodeHex = request.params[2].get_str();
    if (scriptCodeHex.empty() || !IsHex(scriptCodeHex)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "from_scriptcode_hex must be valid hex");
    }
    std::vector<unsigned char> scriptCodeBytes = ParseHex(scriptCodeHex);
    CScript scriptCode(scriptCodeBytes.begin(), scriptCodeBytes.end());

    // Parse from_pubkey_hex
    std::string pubkeyHex = request.params[3].get_str();
    if (pubkeyHex.empty() || !IsHex(pubkeyHex)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "from_pubkey_hex must be valid hex");
    }
    std::vector<unsigned char> pubkeyBytes = ParseHex(pubkeyHex);
    if (pubkeyBytes.size() != CDilithiumPubKey::SIZE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            strprintf("Public key must be exactly %u bytes", static_cast<unsigned int>(CDilithiumPubKey::SIZE)));
    }

    // Parse from_privkey_hex
    std::vector<unsigned char> privkey = ParseHex(request.params[4].get_str());
    if (privkey.size() != DILITHIUM_SECRETKEYBYTES) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            strprintf("Private key must be exactly %u bytes", DILITHIUM_SECRETKEYBYTES));
    }

    // Parse to_address
    std::string toAddress = request.params[5].get_str();
    CTxDestination dest = DecodeDestination(toAddress);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid destination address");
    }
    CScript outputScriptPubKey = GetScriptForDestination(dest);

    // Parse amount
    CAmount amount = AmountFromValue(request.params[6]);
    if (amount <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Amount must be greater than 0");
    }

    // Parse fee (optional, default 0.0001)
    CAmount fee = 0.0001 * COIN;
    if (request.params.size() > 7 && !request.params[7].isNull()) {
        fee = AmountFromValue(request.params[7]);
    }
    if (fee < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Fee cannot be negative");
    }

    node::NodeContext& node = EnsureAnyNodeContext(request.context);
    ChainstateManager& chainman = EnsureChainman(node);
    const COutPoint outpoint(txid, vout);
    CAmount inputAmount;
    {
        LOCK(cs_main);
        Coin coin = chainman.ActiveChainstate().CoinsTip().AccessCoin(outpoint);
        if (coin.IsSpent()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "UTXO not found or already spent");
        }
        inputAmount = coin.out.nValue;
    }

    CAmount change = inputAmount - amount - fee;
    if (change < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            strprintf("Insufficient funds: input amount %s, required %s (amount %s + fee %s)",
                FormatMoney(inputAmount),
                FormatMoney(amount + fee),
                FormatMoney(amount),
                FormatMoney(fee)));
    }

    CMutableTransaction mtx;
    constexpr uint32_t SEQ_RBF = 0xFFFFFFFD;
    mtx.vin.emplace_back(outpoint, CScript{}, SEQ_RBF);
    mtx.vout.emplace_back(amount, outputScriptPubKey);
    if (change > 0) {
        mtx.vout.emplace_back(change, scriptCode);
    }

    const bool native_pq_witness = IsNativePQWitnessKeyhashScript(scriptCode);
    CScript witness_script;
    if (native_pq_witness) {
        const std::vector<unsigned char> program(scriptCode.begin() + 2, scriptCode.end());
        witness_script << OP_DUP << OP_HASH160 << program << OP_EQUALVERIFY << OP_CHECKSIG;
    }

    const CTransaction tx_for_sighash(mtx);
    int32_t nHashType = SIGHASH_ALL;
    const uint256 sighash = native_pq_witness
        ? SignatureHash(witness_script, tx_for_sighash, 0, nHashType, inputAmount, SigVersion::WITNESS_V0, nullptr)
        : SignatureHash(scriptCode, tx_for_sighash, 0, nHashType, 0, SigVersion::BASE, nullptr);

    valtype signature;
    std::vector<unsigned char> msgHash(sighash.begin(), sighash.end());
    if (!PQ_Sign(signature, msgHash, privkey)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to sign transaction");
    }

    valtype sigWithHashtype = signature;
    sigWithHashtype.push_back(static_cast<unsigned char>(nHashType));

    if (native_pq_witness) {
        mtx.vin[0].scriptSig = CScript{};
        mtx.vin[0].scriptWitness.stack.clear();
        mtx.vin[0].scriptWitness.stack.push_back(sigWithHashtype);
        mtx.vin[0].scriptWitness.stack.push_back(pubkeyBytes);
    } else {
        CScript scriptSig;
        scriptSig << sigWithHashtype << pubkeyBytes;
        mtx.vin[0].scriptSig = scriptSig;
        mtx.vin[0].scriptWitness.stack.clear();
    }

    const std::string scriptSig_hex = HexStr(mtx.vin[0].scriptSig);
    const int64_t witness_items = static_cast<int64_t>(mtx.vin[0].scriptWitness.stack.size());
    const int64_t witness0_size =
        mtx.vin[0].scriptWitness.stack.size() > 0 ? static_cast<int64_t>(mtx.vin[0].scriptWitness.stack[0].size()) : 0;
    const int64_t witness1_size =
        mtx.vin[0].scriptWitness.stack.size() > 1 ? static_cast<int64_t>(mtx.vin[0].scriptWitness.stack[1].size()) : 0;

    PqSendtoSigned out;
    out.tx = MakeTransactionRef(std::move(mtx));
    out.to_address = std::move(toAddress);
    out.amount = amount;
    out.fee = fee;
    out.change = change;
    out.native_pq_witness = native_pq_witness;
    out.scriptSig_hex = std::move(scriptSig_hex);
    out.witness_items = witness_items;
    out.witness0_size = witness0_size;
    out.witness1_size = witness1_size;
    return out;
}

static RPCHelpMan pqcreaterawspend()
{
    return RPCHelpMan{"pqcreaterawspend",
        "\nBuild and sign a raw PQ (Dilithium) spend transaction without broadcasting (regtest only).\n"
        "Same arguments and signing rules as pqsendto.\n",
        {
            {"from_txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction ID (hex) of the UTXO being spent"},
            {"from_vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output index (vout) of the UTXO being spent",
                RPCArgOptions{.skip_type_check = true, .type_str = {"", "numeric or string"}}},
            {"from_scriptcode_hex", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The scriptCode hex (scriptPubKey of the UTXO)"},
            {"from_pubkey_hex", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Dilithium3 public key (hex)"},
            {"from_privkey_hex", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Dilithium3 private key (hex)"},
            {"to_address", RPCArg::Type::STR, RPCArg::Optional::NO, "Destination address (base58 PQ address)"},
            {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Amount to send (in QBX)"},
            {"fee", RPCArg::Type::AMOUNT, RPCArg::Default{0.0001}, "Transaction fee (in QBX, default: 0.0001)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "rawtx", "Signed transaction hex"},
                {RPCResult::Type::STR_HEX, "txid", "Transaction id (non-witness)"},
                {RPCResult::Type::STR_HEX, "wtxid", "Witness transaction id"},
                {RPCResult::Type::BOOL, "native_pq_witness", "True if spending OP_2 <20> native PQ witness"},
                {RPCResult::Type::STR_HEX, "scriptSig_hex", "Hex of input 0 scriptSig"},
                {RPCResult::Type::NUM, "witness_items", "Number of witness stack items on input 0"},
                {RPCResult::Type::NUM, "witness0_size", "Size in bytes of witness stack item 0"},
                {RPCResult::Type::NUM, "witness1_size", "Size in bytes of witness stack item 1"},
                {RPCResult::Type::NUM, "fee", "Transaction fee"},
                {RPCResult::Type::NUM, "amount", "Amount sent to destination"},
                {RPCResult::Type::NUM, "change", "Change to sender script (0 if none)"},
            }
        },
        RPCExamples{
            HelpExampleCli("pqcreaterawspend", "\"<txid>\" 0 \"<scriptcode_hex>\" \"<pubkey_hex>\" \"<privkey_hex>\" \"<address>\" 1.0")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            if (!Params().IsMockableChain()) {
                throw std::runtime_error("pqcreaterawspend is for regression testing (-regtest mode) only");
            }
            const PqSendtoSigned s = PqBuildSignedSendtoTx(request);
            UniValue result(UniValue::VOBJ);
            result.pushKV("rawtx", EncodeHexTx(*s.tx));
            result.pushKV("txid", s.tx->GetHash().GetHex());
            result.pushKV("wtxid", s.tx->GetWitnessHash().GetHex());
            result.pushKV("native_pq_witness", s.native_pq_witness);
            result.pushKV("scriptSig_hex", s.scriptSig_hex);
            result.pushKV("witness_items", s.witness_items);
            result.pushKV("witness0_size", s.witness0_size);
            result.pushKV("witness1_size", s.witness1_size);
            result.pushKV("fee", ValueFromAmount(s.fee));
            result.pushKV("amount", ValueFromAmount(s.amount));
            result.pushKV("change", ValueFromAmount(s.change));
            return result;
        },
    };
}

static RPCHelpMan pqsendto()
{
    return RPCHelpMan{"pqsendto",
        "\nSend PQ (Dilithium) coins from a UTXO to an address (regtest only).\n"
        "Builds, signs, and broadcasts a transaction without using the wallet.\n",
        {
            {"from_txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction ID (hex) of the UTXO being spent"},
            {"from_vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output index (vout) of the UTXO being spent"},
            {"from_scriptcode_hex", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The scriptCode hex (scriptPubKey of the UTXO)"},
            {"from_pubkey_hex", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Dilithium3 public key (hex)"},
            {"from_privkey_hex", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Dilithium3 private key (hex)"},
            {"to_address", RPCArg::Type::STR, RPCArg::Optional::NO, "Destination address (base58 PQ address)"},
            {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Amount to send (in QBX)"},
            {"fee", RPCArg::Type::AMOUNT, RPCArg::Default{0.0001}, "Transaction fee (in QBX, default: 0.0001)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "txid", "The transaction ID"},
                {RPCResult::Type::STR_HEX, "rawtx", "The raw transaction hex"},
                {RPCResult::Type::STR, "to", "Destination address"},
                {RPCResult::Type::NUM, "amount", "Amount sent"},
                {RPCResult::Type::NUM, "fee", "Transaction fee"},
                {RPCResult::Type::NUM, "change", "Change amount returned to sender (if any)"},
            }
        },
        RPCExamples{
            HelpExampleCli("pqsendto", "\"<txid>\" 0 \"<scriptcode_hex>\" \"<pubkey_hex>\" \"<privkey_hex>\" \"<address>\" 1.0")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            if (!Params().IsMockableChain()) {
                throw std::runtime_error("pqsendto is for regression testing (-regtest mode) only");
            }

            PqSendtoSigned s = PqBuildSignedSendtoTx(request);
            node::NodeContext& node = EnsureAnyNodeContext(request.context);
            std::string err_string;
            const CAmount max_tx_fee = MAX_MONEY;
            node::TransactionError err = node::BroadcastTransaction(node, s.tx, err_string, max_tx_fee, /*relay=*/true, /*wait_callback=*/true);
            if (err != node::TransactionError::OK) {
                throw JSONRPCTransactionError(err, err_string);
            }

            const Txid txid_result = s.tx->GetHash();
            const Wtxid wtxid_result = s.tx->GetWitnessHash();
            const std::string rawtxHex = EncodeHexTx(*s.tx);

            UniValue result(UniValue::VOBJ);
            result.pushKV("txid", txid_result.GetHex());
            result.pushKV("rawtx", rawtxHex);
            result.pushKV("to", s.to_address);
            result.pushKV("amount", ValueFromAmount(s.amount));
            result.pushKV("fee", ValueFromAmount(s.fee));
            if (s.change > 0) {
                result.pushKV("change", ValueFromAmount(s.change));
            }
            result.pushKV("debug_native_pq_witness", s.native_pq_witness);
            result.pushKV("debug_scriptSig_hex", s.scriptSig_hex);
            result.pushKV("debug_witness_items", s.witness_items);
            result.pushKV("debug_witness0_size", s.witness0_size);
            result.pushKV("debug_witness1_size", s.witness1_size);
            result.pushKV("debug_txid", txid_result.GetHex());
            result.pushKV("debug_wtxid", wtxid_result.GetHex());
            return result;
        },
    };
}

static RPCHelpMan pqcreatemultisig()
{
    return RPCHelpMan{"pqcreatemultisig",
        "\nCreate a Dilithium multisig script template (regtest only).\n"
        "Returns a scriptPubKey for a k-of-n Dilithium multisig.\n",
        {
            {"m", RPCArg::Type::NUM, RPCArg::Optional::NO, "The number of required signatures (1 <= m <= n)."},
            {"pubkeys", RPCArg::Type::ARR, RPCArg::Optional::NO, "A json array of Dilithium public keys (hex strings).",
                {
                    {"pubkey", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "Dilithium public key (hex)"},
                }},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::NUM, "m", "The number of required signatures"},
                {RPCResult::Type::NUM, "n", "The total number of public keys"},
                {RPCResult::Type::ARR, "pubkeys", "Array of public keys",
                {
                    {RPCResult::Type::STR_HEX, "", "Public key hex"},
                }},
                {RPCResult::Type::STR_HEX, "scriptPubKey", "The multisig scriptPubKey in hex format"},
                {RPCResult::Type::STR, "desc", "Descriptor string (raw(...))"},
            }
        },
        RPCExamples{
            "\nCreate a 2-of-3 Dilithium multisig\n"
            + HelpExampleCli("pqcreatemultisig", "2 '[\"<pubkey1>\", \"<pubkey2>\", \"<pubkey3>\"]'") +
            "\nGenerate blocks to the multisig descriptor\n"
            + HelpExampleCli("generatetodescriptor", "1 \"<desc_from_pqcreatemultisig>\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            if (!Params().IsMockableChain()) {
                throw std::runtime_error("pqcreatemultisig is for regression testing (-regtest mode) only");
            }

            // Parse m (required signatures)
            int m = request.params[0].getInt<int>();
            if (m < 1) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "m must be at least 1");
            }

            // Parse pubkeys array
            const UniValue& pubkeys_param = request.params[1];
            if (!pubkeys_param.isArray()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "pubkeys must be an array");
            }

            std::vector<std::vector<unsigned char>> pubkeys;
            for (size_t i = 0; i < pubkeys_param.size(); i++) {
                const std::string& pubkey_hex = pubkeys_param[i].get_str();
                if (!IsHex(pubkey_hex)) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Pubkey %zu is not valid hex", i));
                }
                valtype pubkey_bytes = ParseHex(pubkey_hex);
                
                // Validate Dilithium pubkey using helper
                if (!IsValidDilithiumPubKey(pubkey_bytes)) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, 
                        strprintf("Pubkey %zu has invalid size %zu (expected %zu bytes)", 
                                  i, pubkey_bytes.size(), CDilithiumPubKey::SIZE));
                }
                
                // Additional validation: check that CDilithiumPubKey accepts it
                CDilithiumPubKey dil_pubkey(pubkey_bytes);
                if (!dil_pubkey.IsValid()) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Pubkey %zu is invalid", i));
                }
                
                // Basic format check: reject all-zero pubkeys
                bool all_zero = true;
                for (unsigned char b : pubkey_bytes) {
                    if (b != 0) {
                        all_zero = false;
                        break;
                    }
                }
                if (all_zero) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Pubkey %zu is all zeros (invalid)", i));
                }
                
                pubkeys.push_back(std::move(pubkey_bytes));
            }

            int n = static_cast<int>(pubkeys.size());
            if (n < 1) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "At least one pubkey is required");
            }
            if (n > MAX_PUBKEYS_PER_MULTISIG) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, 
                    strprintf("Too many pubkeys: %d (maximum %d)", n, MAX_PUBKEYS_PER_MULTISIG));
            }
            if (m > n) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, 
                    strprintf("m (%d) cannot be greater than n (%d)", m, n));
            }

            // Construct the multisig script
            CScript scriptPubKey = GetScriptForDilithiumMultisig(m, pubkeys);
            std::string scriptPubKey_hex = HexStr(scriptPubKey);
            
            // Create descriptor
            std::string desc = "raw(" + scriptPubKey_hex + ")";

            // Build result
            UniValue result(UniValue::VOBJ);
            result.pushKV("m", m);
            result.pushKV("n", n);
            
            UniValue pubkeys_array(UniValue::VARR);
            for (const auto& pubkey : pubkeys) {
                pubkeys_array.push_back(HexStr(pubkey));
            }
            result.pushKV("pubkeys", pubkeys_array);
            result.pushKV("scriptPubKey", scriptPubKey_hex);
            result.pushKV("desc", desc);
            
            return result;
        },
    };
}

#ifdef ENABLE_WALLET
static RPCHelpMan pqaddmultisigaddress()
{
    return RPCHelpMan{"pqaddmultisigaddress",
        "\nAdd a Dilithium multisig script to the wallet as watch-only (regtest only).\n"
        "This imports the multisig descriptor into the wallet so it can track transactions.\n"
        "Requires a descriptor wallet.\n",
        {
            {"m", RPCArg::Type::NUM, RPCArg::Optional::NO, "The number of required signatures (1 <= m <= n)."},
            {"pubkeys", RPCArg::Type::ARR, RPCArg::Optional::NO, "A json array of Dilithium public keys (hex strings).",
                {
                    {"pubkey", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "Dilithium public key (hex)"},
                }},
            {"label", RPCArg::Type::STR, RPCArg::Default{""}, "An optional label for the multisig address."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "scriptPubKey", "The multisig scriptPubKey in hex format"},
                {RPCResult::Type::STR, "desc", "Descriptor string (raw(...))"},
            }
        },
        RPCExamples{
            "\nAdd a 2-of-3 Dilithium multisig to wallet\n"
            + HelpExampleCli("pqaddmultisigaddress", "2 '[\"<pubkey1>\", \"<pubkey2>\", \"<pubkey3>\"]' \"mylabel\"")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
        {
            if (!Params().IsMockableChain()) {
                throw std::runtime_error("pqaddmultisigaddress is for regression testing (-regtest mode) only");
            }

            std::shared_ptr<wallet::CWallet> const pwallet = wallet::GetWalletForJSONRPCRequest(request);
            if (!pwallet) {
                throw JSONRPCError(RPC_WALLET_NOT_FOUND, "No wallet selected");
            }

            // Check if wallet is a descriptor wallet
            if (!pwallet->IsWalletFlagSet(wallet::WALLET_FLAG_DESCRIPTORS)) {
                throw JSONRPCError(RPC_WALLET_ERROR, "pqaddmultisigaddress requires a descriptor wallet. Use createwallet with descriptors=true.");
            }

            // Parse label
            const std::string label{wallet::LabelFromValue(request.params[2])};

            // First, create the multisig using pqcreatemultisig logic
            // Parse m (required signatures)
            int m = request.params[0].getInt<int>();
            if (m < 1) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "m must be at least 1");
            }

            // Parse pubkeys array
            const UniValue& pubkeys_param = request.params[1];
            if (!pubkeys_param.isArray()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "pubkeys must be an array");
            }

            std::vector<std::vector<unsigned char>> pubkeys;
            for (size_t i = 0; i < pubkeys_param.size(); i++) {
                const std::string& pubkey_hex = pubkeys_param[i].get_str();
                if (!IsHex(pubkey_hex)) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Pubkey %zu is not valid hex", i));
                }
                valtype pubkey_bytes = ParseHex(pubkey_hex);
                
                // Validate Dilithium pubkey using helper
                if (!IsValidDilithiumPubKey(pubkey_bytes)) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, 
                        strprintf("Pubkey %zu has invalid size %zu (expected %zu bytes)", 
                                  i, pubkey_bytes.size(), CDilithiumPubKey::SIZE));
                }
                
                // Additional validation: check that CDilithiumPubKey accepts it
                CDilithiumPubKey dil_pubkey(pubkey_bytes);
                if (!dil_pubkey.IsValid()) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Pubkey %zu is invalid", i));
                }
                
                // Basic format check: reject all-zero pubkeys
                bool all_zero = true;
                for (unsigned char b : pubkey_bytes) {
                    if (b != 0) {
                        all_zero = false;
                        break;
                    }
                }
                if (all_zero) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Pubkey %zu is all zeros (invalid)", i));
                }
                
                pubkeys.push_back(std::move(pubkey_bytes));
            }

            int n = static_cast<int>(pubkeys.size());
            if (n < 1) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "At least one pubkey is required");
            }
            if (n > MAX_PUBKEYS_PER_MULTISIG) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, 
                    strprintf("Too many pubkeys: %d (maximum %d)", n, MAX_PUBKEYS_PER_MULTISIG));
            }
            if (m > n) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, 
                    strprintf("m (%d) cannot be greater than n (%d)", m, n));
            }

            // Construct the multisig script
            CScript scriptPubKey = GetScriptForDilithiumMultisig(m, pubkeys);
            std::string scriptPubKey_hex = HexStr(scriptPubKey);
            
            // Create descriptor
            std::string desc = "raw(" + scriptPubKey_hex + ")";

            // Import descriptor into wallet as watch-only
            pwallet->BlockUntilSyncedToCurrentChain();
            
            wallet::WalletRescanReserver reserver(*pwallet);
            if (!reserver.reserve(/*with_passphrase=*/true)) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is currently rescanning. Abort existing rescan or wait.");
            }

            LOCK(pwallet->m_relock_mutex);
            wallet::EnsureWalletIsUnlocked(*pwallet);

            // Get current blockchain time for timestamp
            int64_t now = 0;
            int64_t lowest_timestamp = 0;
            CHECK_NONFATAL(pwallet->chain().findBlock(pwallet->GetLastBlockHash(), FoundBlock().time(lowest_timestamp).mtpTime(now)));
            int64_t timestamp = std::max(now, static_cast<int64_t>(1));

            LOCK(pwallet->cs_wallet);

            // Parse descriptor
            FlatSigningProvider keys;  // Empty keys = watch-only
            std::string error;
            auto parsed_descs = Parse(desc, keys, error, /* require_checksum = */ false);
            if (parsed_descs.empty()) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Failed to parse descriptor: %s", error));
            }

            // Note: AddWalletDescriptor may require private keys for wallets with private keys enabled
            // For watch-only import, the wallet should have WALLET_FLAG_DISABLE_PRIVATE_KEYS set
            // Otherwise, we return the descriptor for manual import via importdescriptors
            if (!pwallet->IsWalletFlagSet(wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
                // Wallet has private keys enabled - can't import watch-only directly
                // Return descriptor for manual import
                UniValue result(UniValue::VOBJ);
                result.pushKV("scriptPubKey", scriptPubKey_hex);
                result.pushKV("desc", desc);
                result.pushKV("note", "Wallet has private keys enabled. Use importdescriptors to import this descriptor as watch-only.");
                return result;
            }

            auto parsed_desc = std::move(parsed_descs[0]);
            wallet::WalletDescriptor w_desc(std::move(parsed_desc), timestamp, /*range_start=*/0, /*range_end=*/0, /*next_index=*/0);

            // Add descriptor to the wallet (watch-only since keys is empty and wallet is watch-only)
            auto spk_manager = pwallet->AddWalletDescriptor(w_desc, keys, label, /*internal=*/false);
            if (spk_manager == nullptr) {
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Could not add descriptor '%s'", desc));
            }

            UniValue result(UniValue::VOBJ);
            result.pushKV("scriptPubKey", scriptPubKey_hex);
            result.pushKV("desc", desc);
            
            return result;
        },
    };
}
#endif // ENABLE_WALLET

void RegisterPQRPCCommands(CRPCTable& t)
{
    static const CRPCCommand commands[]{
        {"pq", &pqkeypair},
        {"pq", &pqsighash},
        {"pq", &pqsign},
        {"pq", &pqgetdescriptor},
        {"pq", &pqsetinputscript},
        {"pq", &pqcreaterawspend},
        {"pq", &pqsendto},
        {"pq", &pqcreatemultisig},
#ifdef ENABLE_WALLET
        {"pq", &pqaddmultisigaddress},
#endif
    };
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}


