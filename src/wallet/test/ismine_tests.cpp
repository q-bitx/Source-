// Copyright (c) 2017-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <crypto/dilithium.h>
#include <key.h>
#include <key_io.h>
#include <node/context.h>
#include <script/interpreter.h>
#include <script/pq_script.h>
#include <script/script.h>
#include <script/solver.h>
#include <script/signingprovider.h>
#include <test/util/setup_common.h>
#include <test/util/transaction_utils.h>
#include <wallet/types.h>
#include <wallet/wallet.h>
#include <wallet/test/util.h>

#include <boost/test/unit_test.hpp>

using namespace util::hex_literals;

namespace wallet {
BOOST_FIXTURE_TEST_SUITE(ismine_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(ismine_standard)
{
    CKey keys[2];
    CPubKey pubkeys[2];
    for (int i = 0; i < 2; i++) {
        keys[i].MakeNewKey(true);
        pubkeys[i] = keys[i].GetPubKey();
    }

    CKey uncompressedKey = GenerateRandomKey(/*compressed=*/false);
    CPubKey uncompressedPubkey = uncompressedKey.GetPubKey();
    std::unique_ptr<interfaces::Chain>& chain = m_node.chain;

    CScript scriptPubKey;
    isminetype result;

    // P2PK compressed - Legacy
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        keystore.SetupLegacyScriptPubKeyMan();
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);
        scriptPubKey = GetScriptForRawPubKey(pubkeys[0]);

        // Keystore does not have key
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);

        // Keystore has key
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(keys[0]));
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 1);
    }

    // P2PK compressed - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "pk(" + EncodeSecret(keys[0]) + ")";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        scriptPubKey = GetScriptForRawPubKey(pubkeys[0]);
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);
    }

    // P2PK uncompressed - Legacy
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        keystore.SetupLegacyScriptPubKeyMan();
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);
        scriptPubKey = GetScriptForRawPubKey(uncompressedPubkey);

        // Keystore does not have key
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);

        // Keystore has key
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(uncompressedKey));
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 1);
    }

    // P2PK uncompressed - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "pk(" + EncodeSecret(uncompressedKey) + ")";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        scriptPubKey = GetScriptForRawPubKey(uncompressedPubkey);
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);
    }

    // P2PKH compressed - Legacy
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        keystore.SetupLegacyScriptPubKeyMan();
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);
        scriptPubKey = GetScriptForDestination(PKHash(pubkeys[0]));

        // Keystore does not have key
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);

        // Keystore has key
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(keys[0]));
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 1);
    }

    // P2PKH compressed - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "pkh(" + EncodeSecret(keys[0]) + ")";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        scriptPubKey = GetScriptForDestination(PKHash(pubkeys[0]));
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);
    }

    // P2PKH uncompressed - Legacy
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        keystore.SetupLegacyScriptPubKeyMan();
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);
        scriptPubKey = GetScriptForDestination(PKHash(uncompressedPubkey));

        // Keystore does not have key
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);

        // Keystore has key
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(uncompressedKey));
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 1);
    }

    // P2PKH uncompressed - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "pkh(" + EncodeSecret(uncompressedKey) + ")";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        scriptPubKey = GetScriptForDestination(PKHash(uncompressedPubkey));
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);
    }

    // P2SH - Legacy
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        keystore.SetupLegacyScriptPubKeyMan();
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);

        CScript redeemScript = GetScriptForDestination(PKHash(pubkeys[0]));
        scriptPubKey = GetScriptForDestination(ScriptHash(redeemScript));

        // Keystore does not have redeemScript or key
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);

        // Keystore has redeemScript but no key
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(redeemScript));
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);

        // Keystore has redeemScript and key
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(keys[0]));
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 1);
    }

    // P2SH - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "sh(pkh(" + EncodeSecret(keys[0]) + "))";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        CScript redeemScript = GetScriptForDestination(PKHash(pubkeys[0]));
        scriptPubKey = GetScriptForDestination(ScriptHash(redeemScript));
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);
    }

    // (P2PKH inside) P2SH inside P2SH (invalid) - Legacy
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        keystore.SetupLegacyScriptPubKeyMan();
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);

        CScript redeemscript_inner = GetScriptForDestination(PKHash(pubkeys[0]));
        CScript redeemscript = GetScriptForDestination(ScriptHash(redeemscript_inner));
        scriptPubKey = GetScriptForDestination(ScriptHash(redeemscript));

        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(redeemscript));
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(redeemscript_inner));
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(scriptPubKey));
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(keys[0]));
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);
    }

    // (P2PKH inside) P2SH inside P2SH (invalid) - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "sh(sh(" + EncodeSecret(keys[0]) + "))";

        auto spk_manager = CreateDescriptor(keystore, desc_str, false);
        BOOST_CHECK_EQUAL(spk_manager, nullptr);
    }

    // (P2PKH inside) P2SH inside P2WSH (invalid) - Legacy
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        keystore.SetupLegacyScriptPubKeyMan();
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);

        CScript redeemscript = GetScriptForDestination(PKHash(pubkeys[0]));
        CScript witnessscript = GetScriptForDestination(ScriptHash(redeemscript));
        scriptPubKey = GetScriptForDestination(WitnessV0ScriptHash(witnessscript));

        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(witnessscript));
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(redeemscript));
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(scriptPubKey));
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(keys[0]));
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);
    }

    // (P2PKH inside) P2SH inside P2WSH (invalid) - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "wsh(sh(" + EncodeSecret(keys[0]) + "))";

        auto spk_manager = CreateDescriptor(keystore, desc_str, false);
        BOOST_CHECK_EQUAL(spk_manager, nullptr);
    }

    // P2WPKH inside P2WSH (invalid) - Legacy
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        keystore.SetupLegacyScriptPubKeyMan();
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);

        CScript witnessscript = GetScriptForDestination(WitnessV0KeyHash(pubkeys[0]));
        scriptPubKey = GetScriptForDestination(WitnessV0ScriptHash(witnessscript));

        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(witnessscript));
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(scriptPubKey));
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(keys[0]));
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);
    }

    // P2WPKH inside P2WSH (invalid) - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "wsh(wpkh(" + EncodeSecret(keys[0]) + "))";

        auto spk_manager = CreateDescriptor(keystore, desc_str, false);
        BOOST_CHECK_EQUAL(spk_manager, nullptr);
    }

    // (P2PKH inside) P2WSH inside P2WSH (invalid) - Legacy
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        keystore.SetupLegacyScriptPubKeyMan();
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);

        CScript witnessscript_inner = GetScriptForDestination(PKHash(pubkeys[0]));
        CScript witnessscript = GetScriptForDestination(WitnessV0ScriptHash(witnessscript_inner));
        scriptPubKey = GetScriptForDestination(WitnessV0ScriptHash(witnessscript));

        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(witnessscript_inner));
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(witnessscript));
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(scriptPubKey));
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(keys[0]));
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);
    }

    // (P2PKH inside) P2WSH inside P2WSH (invalid) - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "wsh(wsh(" + EncodeSecret(keys[0]) + "))";

        auto spk_manager = CreateDescriptor(keystore, desc_str, false);
        BOOST_CHECK_EQUAL(spk_manager, nullptr);
    }

    // P2WPKH compressed - Legacy
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        keystore.SetupLegacyScriptPubKeyMan();
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(keys[0]));

        scriptPubKey = GetScriptForDestination(WitnessV0KeyHash(pubkeys[0]));

        // Keystore implicitly has key and P2SH redeemScript
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(scriptPubKey));
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 1);
    }

    // P2WPKH compressed - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "wpkh(" + EncodeSecret(keys[0]) + ")";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        scriptPubKey = GetScriptForDestination(WitnessV0KeyHash(pubkeys[0]));
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);
    }

    // P2WPKH uncompressed - Legacy
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        keystore.SetupLegacyScriptPubKeyMan();
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(uncompressedKey));

        scriptPubKey = GetScriptForDestination(WitnessV0KeyHash(uncompressedPubkey));

        // Keystore has key, but no P2SH redeemScript
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);

        // Keystore has key and P2SH redeemScript
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(scriptPubKey));
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);
    }

    // P2WPKH uncompressed (invalid) - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "wpkh(" + EncodeSecret(uncompressedKey) + ")";

        auto spk_manager = CreateDescriptor(keystore, desc_str, false);
        BOOST_CHECK_EQUAL(spk_manager, nullptr);
    }

    // scriptPubKey multisig - Legacy
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        keystore.SetupLegacyScriptPubKeyMan();
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);

        scriptPubKey = GetScriptForMultisig(2, {uncompressedPubkey, pubkeys[1]});

        // Keystore does not have any keys
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);

        // Keystore has 1/2 keys
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(uncompressedKey));

        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);

        // Keystore has 2/2 keys
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(keys[1]));

        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);

        // Keystore has 2/2 keys and the script
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(scriptPubKey));

        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);
    }

    // scriptPubKey multisig - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "multi(2," + EncodeSecret(uncompressedKey) + "," + EncodeSecret(keys[1]) + ")";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        scriptPubKey = GetScriptForMultisig(2, {uncompressedPubkey, pubkeys[1]});
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);
    }

    // P2SH multisig - Legacy
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        keystore.SetupLegacyScriptPubKeyMan();
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(uncompressedKey));
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(keys[1]));

        CScript redeemScript = GetScriptForMultisig(2, {uncompressedPubkey, pubkeys[1]});
        scriptPubKey = GetScriptForDestination(ScriptHash(redeemScript));

        // Keystore has no redeemScript
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);

        // Keystore has redeemScript
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(redeemScript));
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 1);
    }

    // P2SH multisig - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());

        std::string desc_str = "sh(multi(2," + EncodeSecret(uncompressedKey) + "," + EncodeSecret(keys[1]) + "))";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        CScript redeemScript = GetScriptForMultisig(2, {uncompressedPubkey, pubkeys[1]});
        scriptPubKey = GetScriptForDestination(ScriptHash(redeemScript));
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);
    }

    // P2WSH multisig with compressed keys - Legacy
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        keystore.SetupLegacyScriptPubKeyMan();
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(keys[0]));
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(keys[1]));

        CScript witnessScript = GetScriptForMultisig(2, {pubkeys[0], pubkeys[1]});
        scriptPubKey = GetScriptForDestination(WitnessV0ScriptHash(witnessScript));

        // Keystore has keys, but no witnessScript or P2SH redeemScript
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);

        // Keystore has keys and witnessScript, but no P2SH redeemScript
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(witnessScript));
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);

        // Keystore has keys, witnessScript, P2SH redeemScript
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(scriptPubKey));
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 1);
    }

    // P2WSH multisig with compressed keys - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());

        std::string desc_str = "wsh(multi(2," + EncodeSecret(keys[0]) + "," + EncodeSecret(keys[1]) + "))";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        CScript redeemScript = GetScriptForMultisig(2, {pubkeys[0], pubkeys[1]});
        scriptPubKey = GetScriptForDestination(WitnessV0ScriptHash(redeemScript));
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);
    }

    // P2WSH multisig with uncompressed key - Legacy
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        keystore.SetupLegacyScriptPubKeyMan();
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(uncompressedKey));
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(keys[1]));

        CScript witnessScript = GetScriptForMultisig(2, {uncompressedPubkey, pubkeys[1]});
        scriptPubKey = GetScriptForDestination(WitnessV0ScriptHash(witnessScript));

        // Keystore has keys, but no witnessScript or P2SH redeemScript
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);

        // Keystore has keys and witnessScript, but no P2SH redeemScript
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(witnessScript));
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);

        // Keystore has keys, witnessScript, P2SH redeemScript
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(scriptPubKey));
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);
    }

    // P2WSH multisig with uncompressed key (invalid) - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());

        std::string desc_str = "wsh(multi(2," + EncodeSecret(uncompressedKey) + "," + EncodeSecret(keys[1]) + "))";

        auto spk_manager = CreateDescriptor(keystore, desc_str, false);
        BOOST_CHECK_EQUAL(spk_manager, nullptr);
    }

    // P2WSH multisig wrapped in P2SH - Legacy
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        keystore.SetupLegacyScriptPubKeyMan();
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);

        CScript witnessScript = GetScriptForMultisig(2, {pubkeys[0], pubkeys[1]});
        CScript redeemScript = GetScriptForDestination(WitnessV0ScriptHash(witnessScript));
        scriptPubKey = GetScriptForDestination(ScriptHash(redeemScript));

        // Keystore has no witnessScript, P2SH redeemScript, or keys
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);

        // Keystore has witnessScript and P2SH redeemScript, but no keys
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(redeemScript));
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddCScript(witnessScript));
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);

        // Keystore has keys, witnessScript, P2SH redeemScript
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(keys[0]));
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(keys[1]));
        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 1);
    }

    // P2WSH multisig wrapped in P2SH - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());

        std::string desc_str = "sh(wsh(multi(2," + EncodeSecret(keys[0]) + "," + EncodeSecret(keys[1]) + ")))";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        CScript witnessScript = GetScriptForMultisig(2, {pubkeys[0], pubkeys[1]});
        CScript redeemScript = GetScriptForDestination(WitnessV0ScriptHash(witnessScript));
        scriptPubKey = GetScriptForDestination(ScriptHash(redeemScript));
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);
    }

    // Combo - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());

        std::string desc_str = "combo(" + EncodeSecret(keys[0]) + ")";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        // Test P2PK
        result = spk_manager->IsMine(GetScriptForRawPubKey(pubkeys[0]));
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);

        // Test P2PKH
        result = spk_manager->IsMine(GetScriptForDestination(PKHash(pubkeys[0])));
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);

        // Test P2SH (combo descriptor does not describe P2SH)
        CScript redeemScript = GetScriptForDestination(PKHash(pubkeys[0]));
        scriptPubKey = GetScriptForDestination(ScriptHash(redeemScript));
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);

        // Test P2WPKH
        scriptPubKey = GetScriptForDestination(WitnessV0KeyHash(pubkeys[0]));
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);

        // P2SH-P2WPKH output
        redeemScript = GetScriptForDestination(WitnessV0KeyHash(pubkeys[0]));
        scriptPubKey = GetScriptForDestination(ScriptHash(redeemScript));
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);

        // Test P2TR (combo descriptor does not describe P2TR)
        XOnlyPubKey xpk(pubkeys[0]);
        Assert(xpk.IsFullyValid());
        TaprootBuilder builder;
        builder.Finalize(xpk);
        WitnessV1Taproot output = builder.GetOutput();
        scriptPubKey = GetScriptForDestination(output);
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
    }

    // Taproot - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());

        std::string desc_str = "tr(" + EncodeSecret(keys[0]) + ")";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        XOnlyPubKey xpk(pubkeys[0]);
        Assert(xpk.IsFullyValid());
        TaprootBuilder builder;
        builder.Finalize(xpk);
        WitnessV1Taproot output = builder.GetOutput();
        scriptPubKey = GetScriptForDestination(output);
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_SPENDABLE);
    }

    // OP_RETURN
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        keystore.SetupLegacyScriptPubKeyMan();
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(keys[0]));

        scriptPubKey.clear();
        scriptPubKey << OP_RETURN << ToByteVector(pubkeys[0]);

        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);
    }

    // witness unspendable
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        keystore.SetupLegacyScriptPubKeyMan();
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(keys[0]));

        scriptPubKey.clear();
        scriptPubKey << OP_0 << "aabb"_hex;

        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);
    }

    // witness unknown
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        keystore.SetupLegacyScriptPubKeyMan();
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(keys[0]));

        scriptPubKey.clear();
        scriptPubKey << OP_16 << "aabb"_hex;

        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);
    }

    // Nonstandard
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        keystore.SetupLegacyScriptPubKeyMan();
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(keys[0]));

        scriptPubKey.clear();
        scriptPubKey << OP_9 << OP_ADD << OP_11 << OP_EQUAL;

        result = keystore.GetLegacyScriptPubKeyMan()->IsMine(scriptPubKey);
        BOOST_CHECK_EQUAL(result, ISMINE_NO);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->GetScriptPubKeys().count(scriptPubKey) == 0);
    }
}

BOOST_AUTO_TEST_CASE(ismine_raw_pubkey_matches_spendable_key)
{
    CKey key;
    key.MakeNewKey(true);
    const CScript raw = GetScriptForRawPubKey(key.GetPubKey());
    BOOST_CHECK(raw.back() == OP_CHECKSIG);
    BOOST_CHECK(raw == GetScriptForDestination(PubKeyDestination(key.GetPubKey())));

    std::unique_ptr<interfaces::Chain>& chain = m_node.chain;
    CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
    keystore.SetupLegacyScriptPubKeyMan();
    {
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);
        BOOST_CHECK(keystore.GetLegacyScriptPubKeyMan()->AddKey(key));
        BOOST_CHECK_EQUAL(keystore.GetLegacyScriptPubKeyMan()->IsMine(raw), ISMINE_SPENDABLE);
    }

    CMutableTransaction spend;
    spend.vin.resize(1);
    spend.vin[0].prevout.hash = Txid::FromUint256(uint256::ONE);
    spend.vin[0].prevout.n = 0;
    spend.vout.emplace_back(COIN / 2, raw);
    FillableSigningProvider provider;
    BOOST_CHECK(provider.AddKey(key));
    SignatureData sigdata;
    BOOST_CHECK(SignSignature(provider, raw, spend, 0, COIN, SIGHASH_ALL, sigdata));
    ScriptError err{SCRIPT_ERR_OK};
    BOOST_CHECK(VerifyScript(spend.vin[0].scriptSig, raw, nullptr, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC,
                              MutableTransactionSignatureChecker(&spend, 0, COIN, MissingDataBehavior::ASSERT_FAIL), &err));

    // A 33-byte key under OP_PQCHECKSIG is not a Dilithium spend. Do not report it as ours.
    CScript bogus = CScript() << ToByteVector(key.GetPubKey()) << OP_PQCHECKSIG;
    {
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);
        BOOST_CHECK_EQUAL(keystore.GetLegacyScriptPubKeyMan()->IsMine(bogus), ISMINE_NO);
    }

    valtype dil_pub, dil_priv;
    BOOST_REQUIRE(PQ_GenerateKeypair(dil_pub, dil_priv));
    const CScript legacy = CScript() << OP_DUP << OP_HASH160 << ToByteVector(Hash160(dil_pub)) << OP_EQUALVERIFY << OP_CHECKSIGDILITHIUM;
    BOOST_CHECK(IsPQPayToPubKeyHash(legacy));
    {
        LOCK(keystore.GetLegacyScriptPubKeyMan()->cs_KeyStore);
        BOOST_CHECK_EQUAL(keystore.GetLegacyScriptPubKeyMan()->IsMine(legacy), ISMINE_NO);
    }
    const CScript native = GetScriptForDestination(DilithiumWitnessV0KeyHash(DilithiumPKHash(CKeyID(Hash160(dil_pub)))));
    BOOST_CHECK(native.size() == 22 && native[0] == OP_2);
}

BOOST_AUTO_TEST_CASE(ismine_native_pq_witness_descriptor_wallet)
{
    std::unique_ptr<interfaces::Chain>& chain = m_node.chain;
    CWallet wallet(chain.get(), "", CreateMockableWalletDatabase());
    BOOST_CHECK_EQUAL(wallet.LoadWallet(), DBErrors::LOAD_OK);

    valtype pubkey_bytes, privkey_bytes;
    BOOST_REQUIRE(PQ_GenerateKeypair(pubkey_bytes, privkey_bytes));
    CDilithiumPubKey dilPubKey(pubkey_bytes);
    BOOST_REQUIRE(dilPubKey.IsValid());
    const CKeyID keyid{CKeyID(dilPubKey.GetID())};
    const DilithiumPKHash pk_hash{keyid};
    const CScript witness_spk = GetScriptForDestination(DilithiumWitnessV0KeyHash(pk_hash));

    std::vector<std::vector<unsigned char>> sol;
    BOOST_CHECK(Solver(witness_spk, sol) == TxoutType::DILITHIUM_WITNESS_V0_KEYHASH);

    DescriptorScriptPubKeyMan* dil_spkm = nullptr;
    {
        LOCK(wallet.cs_wallet);
        wallet.SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
        wallet.SetupDescriptorScriptPubKeyMans();
        for (ScriptPubKeyMan* spkm : wallet.GetAllScriptPubKeyMans()) {
            if (auto* d = dynamic_cast<DescriptorScriptPubKeyMan*>(spkm)) {
                dil_spkm = d;
                break;
            }
        }
    }
    BOOST_REQUIRE(dil_spkm != nullptr);
    BOOST_CHECK(dil_spkm->AddDilithiumKeyPubKey(keyid, privkey_bytes, pubkey_bytes));

    {
        LOCK(wallet.cs_wallet);
        BOOST_CHECK_EQUAL(wallet.IsMine(witness_spk), ISMINE_SPENDABLE);
        BOOST_CHECK_EQUAL(wallet.IsMine(CTxDestination{DilithiumWitnessV0KeyHash(pk_hash)}), ISMINE_SPENDABLE);
        BOOST_CHECK(dil_spkm->IsMine(witness_spk) == ISMINE_SPENDABLE);
        BOOST_CHECK(wallet.GetSolvingProvider(witness_spk) != nullptr);
    }
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
