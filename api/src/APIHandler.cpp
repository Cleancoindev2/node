#define TRACE_ENABLER

#include <APIHandler.h>
//#include <DebugLog.h>

#include "csconnector/csconnector.h"

// csdb
#include <csdb/address.h>
#include <csdb/amount.h>
#include <csdb/csdb.h>
#include <csdb/currency.h>
#include <csdb/pool.h>
#include <csdb/storage.h>
#include <csdb/transaction.h>
#include <csdb/wallet.h>

#include <stack>

#include <algorithm>
#include <cassert>
#include <type_traits>

#include <API.h>

#include <lib/system/logger.hpp>

#include <boost/io/ios_state.hpp>
#include <iomanip>
#include <scope_guard.h>

#include <csnode/wallets_cache.hpp>

constexpr csdb::user_field_id_t smart_state_idx = ~1;

using namespace api;
using namespace ::apache;
using namespace Credits;

api::custom::APIProcessor::APIProcessor(
  ::apache::thrift::stdcxx::shared_ptr<APIHandler> iface)
  : api::APIProcessor(iface)
  , ss()
{}

bool
custom::APIProcessor::dispatchCall(::apache::thrift::protocol::TProtocol* iprot,
                                   ::apache::thrift::protocol::TProtocol* oprot,
                                   const std::string& fname,
                                   int32_t seqid,
                                   void* callContext)
{
#ifndef FAKE_API_HANDLING
  TRACE("");
  auto custom_iface_ = std::dynamic_pointer_cast<APIHandler>(iface_);
  TRACE("");
  auto it = custom_iface_->work_queues.find(fname);
  TRACE("");
  if (it != custom_iface_->work_queues.end()) {
    //   TRACE("");
    it->second.get_position();
    //   TRACE("");
  }
  TRACE("");
  ss.leave();
#else
  using namespace std::chrono_literals;
  std::this_thread::sleep_for(200ms);
#endif
  TRACE(std::endl << fname);
  auto res =
    api::APIProcessor::dispatchCall(iprot, oprot, fname, seqid, callContext);
  TRACE(std::endl);
  return res;
}

APIHandler::APIHandler(BlockChain& blockchain, Credits::Solver& _solver)
  : s_blockchain(blockchain)
  , solver(_solver)
  , stats(blockchain)
  , executor_transport(new ::apache::thrift::transport::TBufferedTransport(
      ::apache::thrift::stdcxx::make_shared<
        ::apache::thrift::transport::TSocket>("localhost", 9080)))
  , executor(::apache::thrift::stdcxx::make_shared<
             ::apache::thrift::protocol::TBinaryProtocol>(executor_transport))
{
  TRACE("");
  std::cerr << (s_blockchain.isGood() ? "Storage is opened normal"
                                      : "Storage is not opened")
            << std::endl;
  //  TRACE("");
  if (!s_blockchain.isGood()) {
    return;
  }
  //  TRACE("");
  work_queues["TransactionFlow"]; // init value with default
                                  // constructors
                                  TRACE("");
  auto lapooh = s_blockchain.getLastHash();
  trace = false;
  while (update_smart_caches_once(lapooh, true)) {
    //      TRACE("");
  }
  trace = true;
  //   TRACE("");
  state_updater_running.test_and_set(std::memory_order_acquire);
  state_updater = std::thread([this]() {
    // trace = false;
    //     TRACE("");
    auto lapooh = s_blockchain.getLastHash();
    //    TRACE("");
    while (state_updater_running.test_and_set(std::memory_order_acquire)) {
      if (!update_smart_caches_once(lapooh)) {
                  TRACE("");
        lapooh = s_blockchain.wait_for_block(lapooh);
      }
    }
  });
  //  TRACE("");
}

APIHandler::~APIHandler()
{
  state_updater_running.clear(std::memory_order_release);
  if (state_updater.joinable()) {
    state_updater.join();
  }
}

// void
// APIHandler::smart_rescan(bool init)
//{
//    if (!init) {
//        return;
//    }
//    TRACE("");
//    auto lapooh = s_blockchain.getLastHash();
//    while (update_smart_caches_once(lapooh, init)) {
//        TRACE("");
//    }
//}

void
APIHandlerBase::SetResponseStatus(APIResponse& response,
                                  APIRequestStatusType status,
                                  const std::string& details)
{
  struct APIRequestStatus
  {
    APIRequestStatus(uint8_t code, std::string message)
      : message(message)
      , code(code)
    {}
    std::string message;
    uint8_t code;
  };

  APIRequestStatus
    statuses[static_cast<size_t>(APIHandlerBase::APIRequestStatusType::MAX)] = {
      {
        0,
        "Success",
      },
      {
        1,
        "Failure",
      },
      { 2, "Not Implemented" },
    };
  response.code = statuses[static_cast<uint8_t>(status)].code;
  response.message = statuses[static_cast<uint8_t>(status)].message + details;
}

void
APIHandlerBase::SetResponseStatus(APIResponse& response, bool commandWasHandled)
{
  SetResponseStatus(response,
                    (commandWasHandled
                       ? APIRequestStatusType::SUCCESS
                       : APIRequestStatusType::NOT_IMPLEMENTED));
}

void
APIHandler::BalanceGet(BalanceGetResult& _return,
                       const Address& address,
                       const Currency currency)
{
  csdb::Address addr;
  // if (address.size() != 64)
  addr = BlockChain::getAddressFromKey(address);
  // else
  //    addr = csdb::Address::from_string(address);

  csdb::Amount result = s_blockchain.getBalance(addr);

  _return.amount.integral = result.integral();
  _return.amount.fraction = result.fraction();

  SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

std::string
fromByteArray(const ::csdb::internal::byte_array& bar)
{
  std::string res;
  {
    res.reserve(bar.size());
    std::transform(bar.begin(),
                   bar.end(),
                   std::back_inserter<std::string>(res),
                   [](uint8_t _) { return char(_); });
  }
  return res;
}

::csdb::internal::byte_array
toByteArray(const std::string& s)
{
  ::csdb::internal::byte_array res;
  {
    res.reserve(s.size());
    std::transform(s.begin(),
                   s.end(),
                   std::back_inserter<decltype(res)>(res),
                   [](uint8_t _) { return uint8_t(_); });
  }
  return res;
}

api::Amount
convertAmount(const csdb::Amount& amount)
{
  api::Amount result;
  result.integral = amount.integral();
  result.fraction = amount.fraction();
  assert(result.fraction >= 0);
  return result;
}

api::TransactionId
convert_transaction_id(const csdb::TransactionID& trid)
{
  api::TransactionId result_id;
  result_id.index = trid.index();
  result_id.poolHash = fromByteArray(trid.pool_hash().to_binary());
  return result_id;
}

csdb::TransactionID
convert_transaction_id(const api::TransactionId& trid)
{
  return csdb::TransactionID(
    csdb::PoolHash::from_binary(toByteArray(trid.poolHash)), trid.index);
}

api::SealedTransaction
convertTransaction(const csdb::Transaction& transaction)
{
  api::SealedTransaction result;
  csdb::Amount amount = transaction.amount();
  csdb::Currency currency = transaction.currency();
  csdb::Address target = transaction.target();
  csdb::TransactionID id = transaction.id();
  csdb::Address address = transaction.source();

  result.id.index = id.index();
  result.id.poolHash = fromByteArray(id.pool_hash().to_binary());

  result.trxn.id = transaction.innerID();
  result.trxn.amount = convertAmount(amount);
  result.trxn.currency = DEFAULT_CURRENCY;

  result.trxn.source = fromByteArray(address.public_key());
  result.trxn.target = fromByteArray(target.public_key());

  result.trxn.fee = convertAmount(transaction.counted_fee());

  auto uf = transaction.user_field(0);
  if ((result.trxn.__isset.smartContract = uf.is_valid())) { // non-bug
    result.trxn.smartContract =
      deserialize<api::SmartContractInvocation>(uf.value<std::string>());
  }

  return result;
}

std::vector<api::SealedTransaction>
convertTransactions(const std::vector<csdb::Transaction>& transactions)
{
  std::vector<api::SealedTransaction> result;
  // reserve vs resize
  result.resize(transactions.size());
  std::transform(transactions.begin(),
                 transactions.end(),
                 result.begin(),
                 convertTransaction);
  return result;
}

api::Pool
APIHandler::convertPool(const csdb::Pool& pool)
{
  api::Pool result;
  pool.is_valid();
  if (pool.is_valid()) {
    result.hash = fromByteArray(pool.hash().to_binary());
    result.poolNumber = pool.sequence();
    assert(result.poolNumber >= 0);
    result.prevHash = fromByteArray(pool.previous_hash().to_binary());
    // std::cerr << pool.user_field(0).value<std::string>() << std::endl;
    result.time = atoll(
      pool.user_field(0)
        .value<std::string>()
        .c_str()); // atoll(pool.user_field(0).value<std::string>().c_str());

    result.transactionsCount =
      (int32_t)pool.transactions_count(); // DO NOT EVER CREATE POOLS WITH
                                          // MORE THAN 2 BILLION
                                          // TRANSACTIONS, EVEN AT NIGHT

    auto wpk = pool.writer_public_key();
    result.writer = fromByteArray(wpk);

    double totalFee = 0;
    const auto& transs = const_cast<csdb::Pool&>(pool).transactions();
    for (auto& t : transs)
      totalFee+= t.counted_fee().to_double();


    const auto tf = csdb::Amount(totalFee);
    result.totalFee.integral = tf.integral();
    result.totalFee.fraction = tf.fraction();
  }
  return result;
}

api::Pool
APIHandler::convertPool(const csdb::PoolHash& poolHash)
{
  return convertPool(s_blockchain.loadBlock(poolHash));
}

std::vector<api::SealedTransaction>
extractTransactions(const csdb::Pool& pool, int64_t limit, const int64_t offset)
{
  int64_t transactionsCount = pool.transactions_count();
  assert(transactionsCount >= 0);

  std::vector<api::SealedTransaction> result;

  if (offset > transactionsCount) {
    return result; // если запрашиваемые
                   // транзакций выходят за
    // пределы пула возвращаем пустой результат
  }
  transactionsCount -=
    offset; // мы можем отдать все транзакции в пуле за вычетом смещения

  if (limit > transactionsCount)
    limit = transactionsCount; // лимит уменьшается до реального количества
                               // транзакций которые можно отдать

  for (int64_t index = offset; index < (offset + limit); ++index) {
    result.push_back(convertTransaction(pool.transaction(index)));
  }
  return result;
}

void
APIHandler::TransactionGet(TransactionGetResult& _return,
                           const TransactionId& transactionId)
{
  // Log("TransactionGet");

  csdb::PoolHash poolhash =
    csdb::PoolHash::from_binary(toByteArray(transactionId.poolHash));
  csdb::TransactionID tmpTransactionId =
    csdb::TransactionID(poolhash, (transactionId.index));
  csdb::Transaction transaction =
    s_blockchain.loadTransaction(tmpTransactionId);

  _return.found = transaction.is_valid();
  if (_return.found) {
    _return.transaction = convertTransaction(transaction);
  }

  SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void
APIHandler::TransactionsGet(TransactionsGetResult& _return,
                            const Address& address,
                            int64_t offset,
                            const int64_t limit)
{
  // Log("TransactionsGet");

  csdb::Address addr;
  // if (address.size() != 64)
  addr = BlockChain::getAddressFromKey(address);
  // else
  //    addr = csdb::Address::from_string(address);

  BlockChain::Transactions transactions;

  s_blockchain.getTransactions(transactions, addr, offset, limit);

  _return.transactions = convertTransactions(transactions);

  SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

api::SmartContractInvocation
fetch_smart(const csdb::Transaction& tr)
{
  return tr.is_valid() ? deserialize<api::SmartContractInvocation>(
                           tr.user_field(0).value<std::string>())
                       : api::SmartContractInvocation();
}

api::SmartContract
fetch_smart_body(const csdb::Transaction& tr)
{
  api::SmartContract res;
  if (!tr.is_valid()) {
    return res;
  }
  auto sci = deserialize<api::SmartContractInvocation>(
    tr.user_field(0).value<std::string>());
  res.byteCode = sci.byteCode;
  res.sourceCode = sci.sourceCode;
  res.hashState = sci.hashState;

  res.deployer = fromByteArray(tr.source().public_key());
  res.address = fromByteArray(tr.target().public_key());

  return res;
}

bool
is_smart(const csdb::Transaction& tr)
{
  csdb::UserField uf = tr.user_field(0);
  return uf.type() == csdb::UserField::Type::String;
}

bool
is_smart_deploy(const api::SmartContractInvocation& smart)
{
  return smart.method.empty();
}

csdb::Transaction
APIHandler::make_transaction(const Transaction& transaction)
{
  csdb::Transaction send_transaction;
  PublicKey from, to;

  auto source = BlockChain::getAddressFromKey(transaction.source);

  const uint64_t WALLET_DENOM = 1'000'000'000'000'000'000ull;

    send_transaction.set_amount(csdb::Amount(
      transaction.amount.integral, transaction.amount.fraction, WALLET_DENOM));
    send_transaction.set_balance(s_blockchain.getBalance(source));
    send_transaction.set_currency(csdb::Currency("CS"));
    send_transaction.set_source(source);
    send_transaction.set_target(
      BlockChain::getAddressFromKey(transaction.target));
    send_transaction.set_max_fee(csdb::Amount(
      transaction.fee.integral, transaction.fee.fraction, WALLET_DENOM));
    send_transaction.set_innerID(transaction.id);
    send_transaction.set_signature(transaction.signature);
    return send_transaction;
}

std::string
get_delimited_transaction_sighex(const csdb::Transaction& tr)
{
  auto bs = fromByteArray(tr.to_byte_stream_for_sig());
  return std::string({ ' ' }) + byteStreamToHex(bs.data(), bs.length());
}

void
APIHandler::dumb_transaction_flow(api::TransactionFlowResult& _return,
                                  const Transaction& transaction)
{
  work_queues["TransactionFlow"].yield();
  auto tr = make_transaction(transaction);
  TRACE("");
  solver.send_wallet_transaction(tr);
  SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS, get_delimited_transaction_sighex(tr));
  TRACE("");
}

template<typename T>
std::enable_if<std::is_convertible<T*, ::apache::thrift::TBase*>::value,
               std::ostream&>
operator<<(std::ostream& s, const T& t)
{
  t.printTo(s);
  return s;
}

void
APIHandler::smart_transaction_flow(api::TransactionFlowResult& _return,
                                   const Transaction& transaction)
{
  auto input_smart = transaction.smartContract;
  TRACE(std::endl << "transaction = " << transaction);

  csdb::Transaction send_transaction = make_transaction(transaction);

  const auto smart_addr = send_transaction.target();

  bool deploy = is_smart_deploy(input_smart);
  if (!deploy) {
    input_smart.byteCode = std::string();
    input_smart.sourceCode = std::string();
  }

  TRACE("");

  bool present = false;
  std::string origin_bytecode;
  {

    //   TRACE("");
    decltype(auto) smart_origin = locked_ref(this->smart_origin);
    //   TRACE("");

    auto it = smart_origin->find(smart_addr);
    if ((present = (it != smart_origin->end()))) {
      origin_bytecode =
        fetch_smart(s_blockchain.loadTransaction(it->second)).byteCode;
    }
  }

  if (present == deploy) {

    //   TRACE("");

    SetResponseStatus(_return.status, APIRequestStatusType::FAILURE);
    return;
  }

  const std::string& bytecode = deploy ? input_smart.byteCode : origin_bytecode;

  bool amnesia = input_smart.forgetNewState;

  auto& contract_state_entry = [this, &smart_addr]() -> decltype(auto) {
    TRACE("");
    auto smart_state(locked_ref(this->smart_state));
    TRACE("");

    return (*smart_state)[smart_addr];
  }();

  TRACE("");

  work_queues["TransactionFlow"].wait_till_front([&](std::tuple<>) {
    TRACE("");
    contract_state_entry.get_position();
    TRACE("");
    return true;
  });

  auto inner_id = send_transaction.innerID();

  TRACE("");
  auto &contract_state_record = contract_state_entry.wait_till_front([&](smart_state_record& state) {
        //  TRACE("");
        auto res = state.locker_trx == 0;
        if (res) {
          TRACE("");
#ifndef TETRIS_NODE
          state.locker_trx = inner_id;
#else
          state.locker_trx = (deploy ? 1 : 0);
#endif
        }
        TRACE("");

        return res;
  });

  if (deploy) {
    work_queues["TransactionFlow"].get_position();
    work_queues["TransactionFlow"].wait_till_front([&](std::tuple<>) {
      contract_state_entry.get_position();
      return true;
    });
  }

  bool trxn_sent = false;
  auto sg = scopeGuard([&contract_state_entry, &trxn_sent, amnesia]() {
          TRACE("");
          if (trxn_sent || amnesia) {
                  contract_state_entry.update_state([](smart_state_record& contract_state) {
                          TRACE("");
                          contract_state.locker_trx = 0;
                  });
          }
  });

  executor::APIResponse api_resp;

  TRACE("");

  while (!executor_transport->isOpen()) {
    executor_transport->open();
  }

  TRACE("");
  executor.executeByteCode(api_resp,
                           transaction.source,
                           bytecode,
                           contract_state_record.state,
                           input_smart.method,
                           input_smart.params);

  TRACE("");

  if (api_resp.code) {
    TRACE("");
    _return.status.code = api_resp.code;
    _return.status.message = api_resp.message;
    return;
  }

  if ((_return.__isset.smart_contract_result =
         api_resp.__isset.ret_val)) { // non-bug = instead of ==
    TRACE("");
    _return.smart_contract_result = api_resp.ret_val;
  }

  TRACE("");

  if (amnesia) {
    TRACE("");
    SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
    return;
  }

  TRACE("");

  send_transaction.add_user_field(0, serialize(input_smart));
  send_transaction.add_user_field(smart_state_idx, api_resp.contractState);
  solver.send_wallet_transaction(send_transaction);

  trxn_sent = true;

  SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS, get_delimited_transaction_sighex(send_transaction));

  TRACE("");
  if (deploy) {
    TRACE("");
    contract_state_entry.wait_till_front([&](smart_state_record& state) {
      TRACE("");
      return state.locker_trx == 0;
    });
  }
  else {
#ifndef TETRIS_NODE
          using namespace std::chrono_literals;
          runAfter(10000ms, [&contract_state_entry, inner_id]() {
                  TRACE("");
                  contract_state_entry.update_state([=](smart_state_record& contract_state) {
                          TRACE("");
                          if (contract_state.locker_trx != inner_id) {
                                  return;
                          }
                          TRACE("");
                          contract_state.locker_trx = 0;
                  });
          });
#else
    contract_state_entry.update_state([=](smart_state_record& contract_state) {
                          contract_state.state = api_resp.contractState;
                          contract_state.locker_trx = 0;
          });
#endif
  }
  TRACE("");
}

void
APIHandler::TransactionFlow(api::TransactionFlowResult& _return,
                            const Transaction& transaction)
{

  if (transaction.target == "accXpfvxnZa8txuxpjyPqzBaqYPHqYu2rwn34lL8rjI=") {
    return;
  }

  TRACE("");

  if (!transaction.__isset.smartContract) {
    dumb_transaction_flow(_return, transaction);
  } else {
    smart_transaction_flow(_return, transaction);
  }
}

void
APIHandler::PoolListGet(api::PoolListGetResult& _return,
                        const int64_t offset,
                        const int64_t const_limit)
{
  ////////////////////////std::cout << "PoolListGet: " << offset << ", "
  ///<<
  /// const_limit << std::endl;

  TRACE(offset << " " << const_limit);

  if (offset > 100)
    const_cast<int64_t&>(offset) = 100;
  if (const_limit > 100)
    const_cast<int64_t&>(const_limit) = 100;

  _return.pools.reserve(const_limit);

  csdb::PoolHash hash = s_blockchain.getLastHash();

  size_t lastCount = 0;
  csdb::Pool pool; // = s_blockchain->loadBlock(hash/*, lastCount*/);

  uint64_t sequence = s_blockchain.getSize();

  const uint64_t lower =
    sequence - std::min(sequence, (uint64_t)(offset + const_limit));
  for (uint64_t it = sequence; it > lower; --it) {
    auto cch = poolCache.find(hash);

    if (cch == poolCache.end()) {
      pool = s_blockchain.loadBlock(hash /*, lastCount*/);
      api::Pool apiPool = convertPool(pool);

      if (it <= sequence - std::min(sequence, (uint64_t)offset)) {
        // apiPool.transactionsCount = lastCount;
        _return.pools.push_back(apiPool);
      }
      lastCount = 0;

      poolCache.insert(cch, std::make_pair(hash, apiPool));
      hash = pool.previous_hash();
    } else {
      _return.pools.push_back(cch->second);
      hash = csdb::PoolHash::from_binary(toByteArray(cch->second.prevHash));
    }
  }
}

void
APIHandler::PoolTransactionsGet(PoolTransactionsGetResult& _return,
                                const PoolHash& hash,
                                const int64_t offset,
                                const int64_t limit)
{
  // Log("PoolTransactionsGet");
  csdb::PoolHash poolHash = csdb::PoolHash::from_binary(toByteArray(hash));
  csdb::Pool pool = s_blockchain.loadBlock(poolHash);

  if (pool.is_valid()) {
    _return.transactions = extractTransactions(pool, limit, offset);
  }

  SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void
APIHandler::PoolInfoGet(PoolInfoGetResult& _return,
                        const PoolHash& hash,
                        const int64_t index)
{
  // Log("PoolInfoGet");

  csdb::PoolHash poolHash = csdb::PoolHash::from_binary(toByteArray(hash));
  csdb::Pool pool = s_blockchain.loadBlock(poolHash);
  _return.isFound = pool.is_valid();

  if (_return.isFound) {
    _return.pool = convertPool(poolHash);
  }

  SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void
APIHandler::StatsGet(api::StatsGetResult& _return)
{
  TRACE("StatsGet");

  csstats::StatsPerPeriod stats = this->stats.getStats();

  for (auto& s : stats) {
    api::PeriodStats ps = {};
    ps.periodDuration = s.periodSec;
    ps.poolsCount = s.poolsCount;
    ps.transactionsCount = s.transactionsCount;
    ps.smartContractsCount = s.smartContractsCount;
    ps.transactionsSmartCount = s.transactionsSmartCount;

    for (auto& t : s.balancePerCurrency) {
      api::CumulativeAmount amount;
      amount.integral = t.second.integral;
      amount.fraction = t.second.fraction;
      ps.balancePerCurrency[t.first] = amount;
    }

    _return.stats.push_back(ps);
  }

  SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void
APIHandler::SmartContractGet(api::SmartContractGetResult& _return,
                             const api::Address& address)
{
  // Log("SmartContractGet");

  // std::cerr << "Input address: " << address << std::endl;

  // smart_rescan();

  auto smartrid = [&]() -> decltype(auto) {
    //   TRACE("");
    auto smart_origin = locked_ref(this->smart_origin);
    //   TRACE("");
    auto it = smart_origin->find(BlockChain::getAddressFromKey(address));

    return it == smart_origin->end() ? csdb::TransactionID() : it->second;
  }();
  if (!smartrid.is_valid()) {
    SetResponseStatus(_return.status, APIRequestStatusType::FAILURE);
    return;
  }
  _return.smartContract =
    fetch_smart_body(s_blockchain.loadTransaction(smartrid));

  SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
  //  TRACE("");
}

bool
APIHandler::update_smart_caches_once(const csdb::PoolHash& start, bool init)
{
  auto trace = !init;
  //  TRACE("");
  auto pending_smart_transactions =
    locked_ref(this->pending_smart_transactions);
  //  TRACE("");

  std::vector<csdb::PoolHash> new_blocks;
  auto curph = start;
  while (curph != pending_smart_transactions->last_pull_hash) {
    // LOG_ERROR("pm.hash(): " << curph.to_string());
    new_blocks.push_back(curph);
    size_t _;
    curph = s_blockchain.loadBlockMeta(curph, _).previous_hash();
    if (curph.is_empty()) break;
  }

  if (curph.is_empty() && !pending_smart_transactions->last_pull_hash.is_empty()) {
    // Fork detected!
    auto luca = pending_smart_transactions->last_pull_hash;
    while (!luca.is_empty()) {
      auto fIt = std::find(new_blocks.begin(), new_blocks.end(), luca);
      if (fIt != new_blocks.end()) {
        new_blocks.erase(fIt, new_blocks.end());
        break;
      }

      size_t _;
      luca = s_blockchain.loadBlockMeta(luca, _).previous_hash();
    }
  }

  pending_smart_transactions->last_pull_hash = start;

  while (!new_blocks.empty()) {
    auto trace = false;

    //   TRACE("");
    // LOG_ERROR(
    //  "new_blocks.top().to_string(): " << new_blocks.top().to_string());
    auto p = s_blockchain.loadBlock(new_blocks.back());

    // LOG_ERROR("p.is_valid(): " << p.is_valid());

    new_blocks.pop_back();

    //   TRACE("");

    auto& trs = p.transactions();
    for (auto i_tr = trs.rbegin(); i_tr != trs.rend(); ++i_tr) {
      //    TRACE("");
      auto& tr = *i_tr;
      if (is_smart(tr)) {
        //  TRACE("");
        pending_smart_transactions->queue.push(std::move(tr));
      }
    }
  }
  if (!pending_smart_transactions->queue.empty()) {
    auto tr = std::move(pending_smart_transactions->queue.front());
    pending_smart_transactions->queue.pop();
    //  TRACE(std::endl << convertTransaction(tr));
    auto smart = fetch_smart(tr);
    auto address = tr.target();

    if (!init) {
      auto& e = [&]() -> decltype(auto) {
        auto smart_last_trxn = locked_ref(this->smart_last_trxn);
        return (*smart_last_trxn)[address];
      }();
      std::unique_lock<decltype(e.lock)> l(e.lock);
      e.trid_queue.push_back(tr.id());
      e.new_trxn_cv.notify_all();
    }
    {

      auto& e = [&]() -> decltype(auto) {
        auto smart_state(locked_ref(this->smart_state));
        //    TRACE("");
        return (*smart_state)[address];
      }();
      e.update_state(
        [&tr](smart_state_record& rec) {
                  rec.state = tr.user_field(smart_state_idx).value<std::string>();
                  rec.locker_trx = 0;
          });
    }

    if (is_smart_deploy(smart)) {
      TRACE("");
      {
        auto smart_origin = locked_ref(this->smart_origin);
        (*smart_origin)[address] = tr.id();
      }
      {
        auto deployed_by_creator = locked_ref(this->deployed_by_creator);
        (*deployed_by_creator)[tr.source()].push_back(tr.id());
      }
    }
    return true;
  }
  TRACE("");
  return false;
}

template<typename Mapper>
void
APIHandler::get_mapped_deployer_smart(
  const csdb::Address& deployer,
  Mapper mapper,
  std::vector<decltype(mapper(api::SmartContract()))>& out)
{
  //   TRACE("");

  auto deployed_by_creator = locked_ref(this->deployed_by_creator);

  for (auto& trid : (*deployed_by_creator)[deployer]) {
    auto tr = s_blockchain.loadTransaction(trid);
    auto smart = fetch_smart_body(tr);
    out.push_back(mapper(smart));
  }
}

void
APIHandler::SmartContractsListGet(api::SmartContractsListGetResult& _return,
                                  const api::Address& deployer)
{
  // Log("SmartContractsListGet");

  //  TRACE("");

  csdb::Address addr = BlockChain::getAddressFromKey(deployer);

  // std::cerr << "Input address: " << deployer << std::endl;

  TRACE("");

  get_mapped_deployer_smart(
    addr,
    [](const api::SmartContract& smart) { return smart; },
    _return.smartContractsList);

  TRACE("");

  SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);

  //////////_return.printTo(std::cerr << "SmartContractListGetResult:" <<
  /// std::endl);
  //////////std::cerr << std::endl;
}

void
APIHandler::SmartContractAddressesListGet(
  api::SmartContractAddressesListGetResult& _return,
  const api::Address& deployer)
{
  // Log("SmartContractAddressesListGet");

  csdb::Address addr = BlockChain::getAddressFromKey(deployer);

  get_mapped_deployer_smart(addr,
                            [](const SmartContract& sc) { return sc.address; },
                            _return.addressesList);

  SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void
APIHandler::GetLastHash(api::PoolHash& _return)
{
  _return = fromByteArray(s_blockchain.getLastHash().to_binary());
  return;
}

void
APIHandler::PoolListGetStable(api::PoolListGetResult& _return,
                              const api::PoolHash& hash,
                              const int64_t limit)
{
  ////////////////////////std::cout << "PoolListGet: " << offset << ", "
  ///<<
  /// const_limit << std::endl;

  csdb::PoolHash cur_hash = csdb::PoolHash::from_binary(toByteArray(hash));

  if (limit > 100)
    const_cast<int64_t&>(limit) = 100;

  _return.pools.reserve(limit);

  size_t lastCount = 0;
  csdb::Pool pool; // = s_blockchain->loadBlock(hash/*, lastCount*/);

  for (size_t pools_left = limit; pools_left && !cur_hash.is_empty();
       --pools_left) {
    auto cch = poolCache.find(cur_hash);

    if (cch == poolCache.end()) {
      pool = s_blockchain.loadBlock(cur_hash);
      api::Pool apiPool = convertPool(pool);
      _return.pools.push_back(apiPool);
      lastCount = 0;

      poolCache.insert(cch, std::make_pair(cur_hash, apiPool));
      cur_hash = pool.previous_hash();
    } else {
      _return.pools.push_back(cch->second);
      cur_hash = csdb::PoolHash::from_binary(toByteArray(cch->second.prevHash));
    }
  }
}

void
APIHandler::WaitForSmartTransaction(api::TransactionId& _return,
                                    const api::Address& smart_public)
{
  TRACE(smart_public);
  csdb::Address key = BlockChain::getAddressFromKey(smart_public);

  decltype(smart_last_trxn)::LockedType::iterator it;

  auto& entry = [&]() -> decltype(auto) {
    auto smart_last_trxn = locked_ref(this->smart_last_trxn);
    //  TRACE("");
    std::tie(it, std::ignore) =
      smart_last_trxn->emplace(std::piecewise_construct,
                               std::forward_as_tuple(key),
                               std::forward_as_tuple());
    return std::ref(it->second).get();
  }();
  TRACE("");

  {
    std::unique_lock<decltype(entry.lock)> l(entry.lock);

    ++entry.awaiter_num;

    auto checker = [&]() {
      //  TRACE("");
      if (!entry.trid_queue.empty()) {
        //      TRACE("");
        _return = convert_transaction_id(entry.trid_queue.front());
        //    TRACE("");
        if (--entry.awaiter_num == 0) {
          //        TRACE("");
          entry.trid_queue.pop_front();
        }
        //    TRACE("");
        return true;
      }
      //  TRACE("");

      return false;
    };
    entry.new_trxn_cv.wait(l, checker);
  }
  //  TRACE("");
}

void
APIHandler::SmartContractsAllListGet(SmartContractsListGetResult& _return,
                                     const int64_t _offset,
                                     const int64_t _limit)
{
  // Log("SmartContractsAllListGet");

  // smart_rescan();

  int64_t offset = _offset;
  int64_t limit = _limit;

  //  TRACE("");

  auto smart_origin = locked_ref(this->smart_origin);

  for (auto p : *smart_origin) {
    if (offset) {
      //       TRACE("");
      --offset;
    } else if (limit) {
      //        TRACE("");
      auto trid = p.second;
      auto tr = s_blockchain.loadTransaction(trid);
      _return.smartContractsList.push_back(fetch_smart_body(tr));
      --limit;
    } else
      break;

    //     TRACE("");
  }

  //  TRACE("");

  SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

void
api::APIHandler::WaitForBlock(PoolHash& _return, const PoolHash& obsolete)
{
  _return = fromByteArray(
    s_blockchain
      .wait_for_block(csdb::PoolHash::from_binary(toByteArray(obsolete)))
      .to_binary());
}

void api::APIHandler::SmartMethodParamsGet(SmartMethodParamsGetResult &_return, const Address &address, const int64_t id) {
  csdb::Transaction trx;
  const csdb::Address addr = BlockChain::getAddressFromKey(address);
  if (!s_blockchain.getStorage().get_from_blockchain(addr, id, trx)) {
    SetResponseStatus(_return.status, APIRequestStatusType::FAILURE);
    return;
  }
  _return.method = convertTransaction(trx).trxn.smartContract.method;
  _return.params = convertTransaction(trx).trxn.smartContract.params;
  SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
}

typedef std::list<std::pair<const Credits::WalletsCache::WalletData::Address*,
                            const Credits::WalletsCache::WalletData*>> WCSortedList;
template <typename T>
void walletStep(const Credits::WalletsCache::WalletData::Address* addr,
                const Credits::WalletsCache::WalletData* wd,
                const uint64_t num,
                std::function<const T&(const Credits::WalletsCache::WalletData&)> getter,
                std::function<bool(const T&, const T&)> comparator,
                WCSortedList& lst) {
  assert(num > 0);

  const T& val = getter(*wd);
  if (lst.size() < num || comparator(val, getter(*(lst.back().second)))) {
    // Guess why I can't use std::upper_bound in here
    // C++ is not as expressive as I've imagined it to be...
    auto it = lst.begin();
    while (!comparator(val, getter(*(it->second)))) /* <-- this looks more like Lisp, doesn't it... */
      ++it;

    lst.insert(it, std::make_pair(addr, wd));
    if (lst.size() > num) lst.pop_back();
  }
}

template <typename T>
void iterateOverWallets(std::function<const T&(const Credits::WalletsCache::WalletData&)> getter,
                        const uint64_t num,
                        const bool desc,
                        WCSortedList& lst,
                        BlockChain& bc) {
  std::function<bool(const T&, const T&)> comparator;
  if (desc) comparator = std::greater<T>();
  else comparator = std::less<T>();

  bc.iterateOverWallets([&lst,
                         num,
                         getter,
                         comparator]
                        (const Credits::WalletsCache::WalletData::Address& addr,
                         const Credits::WalletsCache::WalletData& wd) {
                          if (!addr.empty() && wd.balance_ >= csdb::Amount(0))
                            walletStep(&addr,
                                       &wd,
                                       num,
                                       getter,
                                       comparator,
                                       lst);
                          return true;
                        });
}

void
APIHandler::WalletsGet(WalletsGetResult& _return,
                       int64_t _offset,
                       int64_t _limit,
                       int8_t _ordCol,
                       bool _desc) {
  SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);

  WCSortedList lst;
  const uint64_t num = _offset + _limit;

  if (_ordCol == 0) {  // Balance
    iterateOverWallets<csdb::Amount>([](const Credits::WalletsCache::WalletData& wd) -> const csdb::Amount& { return wd.balance_; },
                                     num, _desc, lst, s_blockchain);
  }
#ifdef MONITOR_NODE
  else if (_ordCol == 1) {  // TimeReg
    iterateOverWallets<uint64_t>([](const Credits::WalletsCache::WalletData& wd) -> const uint64_t& { return wd.createTime_; },
                                 num, _desc, lst, s_blockchain);
  }
  else {  // Tx count
    iterateOverWallets<uint64_t>([](const Credits::WalletsCache::WalletData& wd) -> const uint64_t& { return wd.transNum_; },
                                 num, _desc, lst, s_blockchain);
  }
#endif

  if (lst.size() < _offset) return;

  auto ptr = lst.begin();
  std::advance(ptr, _offset);

  for (; ptr != lst.end(); ++ptr) {
    api::WalletInfo wi;
    wi.address = fromByteArray(*(ptr->first));
    wi.balance.integral = ptr->second->balance_.integral();
    wi.balance.fraction = ptr->second->balance_.fraction();
#ifdef MONITOR_NODE
    wi.transactionsNumber = ptr->second->transNum_;
    wi.firstTransactionTime = ptr->second->createTime_;
#endif

    _return.wallets.push_back(wi);
  }
}


void
APIHandler::WritersGet(WritersGetResult& _return, int32_t _page) {
#ifdef MONITOR_NODE
  const static uint32_t PER_PAGE = 256;
  SetResponseStatus(_return.status, APIRequestStatusType::SUCCESS);
  _page = std::max(int32_t(0), _page);

  uint32_t offset = _page * PER_PAGE;
  uint32_t limit = PER_PAGE;
  uint32_t total = 0;

  s_blockchain.iterateOverWriters([&_return, &offset, &limit, &total](const Credits::WalletsCache::WalletData::Address& addr, const Credits::WalletsCache::WriterData& wd) {
                                    if (offset == 0) {
                                      if (limit > 0) {
                                        api::WriterInfo wi;

                                        wi.address = fromByteArray(addr);
                                        wi.timesWriter = wd.times;
                                        wi.feeCollected.integral = wd.totalFee.integral();
                                        wi.feeCollected.fraction = wd.totalFee.fraction();

                                        _return.writers.push_back(wi);
                                        --limit;
                                      }
                                    }
                                    else
                                      --offset;

                                    ++total;
                                    return true;
                                  });
#endif
}
