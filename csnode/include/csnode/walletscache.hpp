#ifndef WALLETS_CACHE_HPP
#define WALLETS_CACHE_HPP

#include <csdb/address.hpp>
#include <csdb/amount.hpp>
#include <csdb/pool.hpp>
#include <csdb/transaction.hpp>
#include <boost/dynamic_bitset.hpp>
#include <csnode/transactionstail.hpp>
#include <cscrypto/cscrypto.hpp>
#include <memory>
#include <vector>
#include <map>

namespace csdb {
class Pool;
class Transaction;
}  // namespace csdb

namespace cs {
class WalletsIds;

class WalletsCache {
public:
  using WalletId = csdb::internal::WalletId;
  using Mask = boost::dynamic_bitset<uint64_t>;

  struct Config {
    size_t initialWalletsNum_ = 2 * 1024 * 1024;
  };

public:
  struct WalletData {
    using Address = std::array<uint8_t, cscrypto::kPublicKeySize>;

    Address address_;
    csdb::Amount balance_;
    TransactionsTail trxTail_;

#ifdef MONITOR_NODE
    uint64_t createTime_ = 0;
    uint64_t transNum_ = 0;
#endif
#ifdef TRANSACTIONS_INDEX
    csdb::TransactionID lastTransaction_;
#endif
  };

  struct WriterData {
    uint64_t times = 0;
    csdb::Amount totalFee;
  };

public:
  static void convert(const csdb::Address& address, WalletData::Address& walletAddress);
  static void convert(const WalletData::Address& walletAddress, csdb::Address& address);

  void iterateOverWallets(const std::function<bool(const WalletData::Address&, const WalletData&)>);

#ifdef MONITOR_NODE
  void iterateOverWriters(const std::function<bool(const WalletData::Address&, const WriterData&)>);
#endif

  uint64_t getCount() const { return wallets_.size(); }

private:
  using Data = std::vector<WalletData*>;

  class ProcessorBase {
  public:
    ProcessorBase(WalletsCache& data)
    : data_(data) {
    }
    virtual ~ProcessorBase() {
    }
    virtual bool findWalletId(const csdb::Address& address, WalletId& id) = 0;

  protected:
    void load(csdb::Pool& curr, const std::vector<std::vector<uint8_t>>& confidants);
    double load(const csdb::Transaction& tr);
    double loadTrxForSource(const csdb::Transaction& tr);
    void fundConfidantsWalletsWithFee(double totalFee, const std::vector<std::vector<uint8_t>>& confidants);
    void loadTrxForTarget(const csdb::Transaction& tr);
    virtual WalletData& getWalletData(WalletId id, const csdb::Address& address) = 0;
    virtual void setModified(WalletId id) = 0;

/*#ifdef MONITOR_NODE
    std::map<WalletData::Address, WriterData> writers_;
#endif*/

  protected:
    static WalletData& getWalletData(Data& wallets, WalletId id, const csdb::Address& address);
#ifdef MONITOR_NODE
    bool setWalletTime(const WalletData::Address& address, const uint64_t &p_timeStamp);
#endif

  protected:
    WalletsCache& data_;
  };

public:
  class Initer : protected ProcessorBase {
  public:
    Initer(WalletsCache& data);
    void loadPrevBlock(csdb::Pool& curr, const std::vector<std::vector<uint8_t>>& confidants);
    bool moveData(WalletId srcIdSpecial, WalletId destIdNormal);
    bool isFinishedOk() const;

  protected:
    bool findWalletId(const csdb::Address& address, WalletId& id) override;
    WalletData& getWalletData(WalletId id, const csdb::Address& address) override;
    void setModified(WalletId id) override;

  protected:
    Data walletsSpecial_;
  };

  class Updater : protected ProcessorBase {
  public:
    Updater(WalletsCache& data);
    void loadNextBlock(csdb::Pool& curr, const std::vector<std::vector<uint8_t>>& confidants);
    const WalletData* findWallet(WalletId id) const;
    const Mask& getModified() const {
      return modified_;
    }

  protected:
    bool findWalletId(const csdb::Address& address, WalletId& id) override;
    WalletData& getWalletData(WalletId id, const csdb::Address& address) override;
    void setModified(WalletId id) override;

  protected:
    Mask modified_;
  };

public:
  WalletsCache(const Config& config, csdb::Address genesisAddress, csdb::Address startAddress, WalletsIds& walletsIds);
  ~WalletsCache();
  WalletsCache(const WalletsCache&) = delete;
  WalletsCache& operator=(const WalletsCache&) = delete;
  WalletsCache(const WalletsCache&&) = delete;
  WalletsCache& operator=(const WalletsCache&&) = delete;

  std::unique_ptr<Initer> createIniter();
  std::unique_ptr<Updater> createUpdater();

private:
  const Config config_;
  WalletsIds& walletsIds_;
  const csdb::Address genesisAddress_;
  const csdb::Address startAddress_;

#ifdef MONITOR_NODE
  std::map<WalletData::Address, WriterData> writers_; 
#endif

  Data wallets_;
};

}  // namespace cs

#endif
