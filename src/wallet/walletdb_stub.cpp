#include <wallet/walletdb.h>
#include <wallet/db.h>
#include <memory>
#include <span>
#include <vector>
#include <string_view>
#include <wallet/walletdb.h>
#include <wallet/wallet.h>
#include <uint256.h>

namespace wallet {

class FakeWalletBatch final : public DatabaseBatch {
public:
    bool ReadKey(DataStream&&, DataStream&) override { return false; }
    bool WriteKey(DataStream&&, DataStream&&, bool = true) override { return false; }
    bool EraseKey(DataStream&&) override { return false; }
    bool HasKey(DataStream&&) override { return false; }
    bool ErasePrefix(std::span<const std::byte>) override { return false; }
    std::unique_ptr<DatabaseCursor> GetNewCursor() override { return nullptr; }
    std::unique_ptr<DatabaseCursor> GetNewPrefixCursor(std::span<const std::byte>) override { return nullptr; }
    bool TxnBegin() override { return true; }
    bool TxnCommit() override { return true; }
    bool TxnAbort() override { return true; }
    bool HasActiveTxn() override { return false; }
    void Flush() override {}
    void Close() override {}
};
bool WalletBatch::ErasePurpose(const std::string& /*dest*/) {
    return true;
}

bool WalletBatch::EraseName(const std::string& /*dest*/) {
    return true;
}

bool WalletBatch::WriteBestBlock(const CBlockLocator& /*locator*/) {
    return true;
}

bool WalletBatch::WriteName(const std::string& /*dest*/, const std::string& /*name*/) {
    return true;
}

bool WalletBatch::WritePurpose(const std::string& /*dest*/, const std::string& /*purpose*/) {
    return true;
}
class FakeWalletDatabase final : public WalletDatabase {
public:
    void Open() override {}
    void AddRef() override {}
    void RemoveRef() override {}
    bool Rewrite(const char* pszSkip = nullptr) override { return true; }
    bool Backup(const std::string& strDest) const override { return true; }
    void Flush() override {}
    void Close() override {}
    bool PeriodicFlush() override { return true; }
    void IncrementUpdateCounter() override {}
    void ReloadDbEnv() override {}
    std::string Filename() override { return "stub"; }
    std::string Format() override { return "stub-format"; }
    std::unique_ptr<DatabaseBatch> MakeBatch(bool flush_on_close = true) override {
        return std::make_unique<FakeWalletBatch>();
    }
    //bool IsDummy() const override { return true; }
};

bool RunWithinTxn(WalletDatabase&, std::string_view, const std::function<bool(WalletBatch&)>&) {
    return true;
}


bool WalletBatch::WritePQPrivateKey(const std::vector<unsigned char>& key) {
    return true;
}

bool WalletBatch::EraseLockedUTXO(const COutPoint& outpoint) {
    return true;
}

DBErrors WalletBatch::LoadWallet(CWallet* pwallet) {
    return DBErrors::LOAD_OK;
}

bool WalletBatch::WriteWalletFlags(uint64_t flags) {
    return true;
}
void WalletBatch::RegisterTxnListener(const DbTxnListener& listener) {
}
bool WalletBatch::WriteAddressReceiveRequest(
    const CTxDestination& dest,
    const std::string& id,
    const std::string& value)
{
    return true;
}
bool WalletBatch::WriteMinVersion(int nVersion) {
    return true;
}

bool WalletBatch::EraseTx(uint256 hash) {
    return true;
}
bool WalletBatch::EraseAddressReceiveRequest(const CTxDestination& dest, const std::string& name) {
    return true;
}

bool WalletBatch::WriteActiveScriptPubKeyMan(uint8_t type, const uint256& id, bool internal) {
    return true;
}

bool WalletBatch::EraseActiveScriptPubKeyMan(uint8_t type, bool internal) {
    return true;
}
bool WalletBatch::TxnBegin() {
    return true;
}

bool WalletBatch::TxnCommit() {
    return true;
}

bool WalletBatch::TxnAbort() {
    return true;
}

bool WalletBatch::WriteMasterKey(unsigned int nID, const CMasterKey& key) {
    return true;
}

bool WalletBatch::WriteTx(const CWalletTx& wtx) {
    return true;
}
bool WalletBatch::ReadBestBlock(CBlockLocator& locator) { return true; }
bool WalletBatch::IsEncrypted() { return false; }
bool WalletBatch::WriteOrderPosNext(int64_t) { return true; }
bool WalletBatch::ReadPool(int64_t, CKeyPool&) { return true; }
bool WalletBatch::WritePool(int64_t, const CKeyPool&) { return true; }
bool WalletBatch::ErasePool(int64_t) { return true; }
bool WalletBatch::WriteDescriptorKey(const uint256&, const CPubKey&, const CPrivKey&) { return true; }
bool WalletBatch::WriteCryptedDescriptorKey(const uint256&, const CPubKey&, const std::vector<unsigned char>&) { return true; }
bool WalletBatch::WriteDescriptor(const uint256&, const WalletDescriptor&) { return true; }
bool WalletBatch::WriteDescriptorDerivedCache(const CExtPubKey&, const uint256&, uint32_t, uint32_t) { return true; }
bool WalletBatch::WriteDescriptorParentCache(const CExtPubKey&, const uint256&, uint32_t) { return true; }
bool WalletBatch::WriteDescriptorLastHardenedCache(const CExtPubKey&, const uint256&, uint32_t) { return true; }
bool WalletBatch::WriteDescriptorCacheItems(const uint256&, const DescriptorCache&) { return true; }
bool WalletBatch::WriteLockedUTXO(const COutPoint&) { return true; }
bool WalletBatch::WriteAddressPreviouslySpent(const CTxDestination&, bool) { return true; }
//bool WalletBatch::WriteAddressReceiveRequest(const CTxDestination&, const std::string&, const std::string&) { return true; }
//bool WalletBatch::EraseAddressReceiveRequest(const CTxDestination&, const std::string&) { return true; }
bool WalletBatch::EraseAddressData(const CTxDestination&) { return true; }
bool WalletBatch::WriteHDChain(const CHDChain&) { return true; }
//bool WalletBatch::WriteWalletFlags(const uint64_t) { return true; }
bool WalletBatch::EraseRecords(const std::unordered_set<std::string>&) { return true; }
//bool WalletBatch::TxnBegin() { return true; }
//bool WalletBatch::TxnCommit() { return true; }
//bool WalletBatch::TxnAbort() { return true; }
//void WalletBatch::RegisterTxnListener(const DbTxnListener&) { }
// Stub for MakeDatabase
std::unique_ptr<WalletDatabase> MakeDatabase(const std::filesystem::path& path,
                                             const DatabaseOptions& options,
                                             DatabaseStatus& status,
                                             bilingual_str& error) {
    return std::make_unique<FakeWalletDatabase>();
}


bool IsBDBFile(const std::filesystem::path&) {
    return false;
}
namespace fs = std::filesystem;
fs::path BDBDataFile(const fs::path& path) {
    return path;
}
std::shared_ptr<CWallet> RestoreWallet(WalletContext&, const std::filesystem::path&, const std::string&, std::optional<bool>, DatabaseStatus&, bilingual_str&, std::vector<bilingual_str>&, bool) {
    return nullptr;
}


bool operator<(std::span<const std::byte>, BytePrefix) {
    return false;
}

bool operator<(BytePrefix, std::span<const std::byte>) {
    return false;
}
}
namespace wallet {

class BerkeleyRODatabase {
public:
    bool Backup(const std::string&) const {
        return true;
    }
};

//std::unique_ptr<BerkeleyRODatabase> MakeBerkeleyRODatabase(const std::filesystem::path&, const DatabaseOptions&, DatabaseStatus&, bilingual_str&) {
  //  return nullptr;
//}
}
namespace wallet {

bool WalletBatch::WriteKey(const CPubKey& vchPubKey, const CPrivKey& vchPrivKey, const CKeyMetadata& keyMeta) {
    return true;
}

bool WalletBatch::WriteCryptedKey(const CPubKey& pubKey, const std::vector<unsigned char>& vchCryptedSecret, const CKeyMetadata& keyMeta) {
    return true;
}

bool WalletBatch::WriteKeyMetadata(const CKeyMetadata& metadata, const CPubKey& pubKey, bool overwrite) {
    return true;
}

bool WalletBatch::WriteCScript(const uint160& hash, const CScript& redeemScript) {
    return true;
}

bool WalletBatch::WriteWatchOnly(const CScript& script, const CKeyMetadata& metadata) {
    return true;
}

bool WalletBatch::EraseWatchOnly(const CScript& script) {
    return true;
}

} // namespace wallet
namespace wallet {
namespace DBKeys {
const std::unordered_set<std::string> LEGACY_TYPES = {};
} // namespace DBKeys
} // namespace wallety

