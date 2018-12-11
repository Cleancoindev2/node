#include <consensus.hpp>
#include <solvercontext.hpp>
#include <states/defaultstatebehavior.hpp>

#pragma warning(push)
//#pragma warning(disable: 4267 4244 4100 4245)
#include <csnode/blockchain.hpp>
#pragma warning(pop)

#include <csdb/pool.hpp>
#include <lib/system/hash.hpp>
#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>

#include <algorithm>

// provide find by sequence() capability
namespace std {
bool operator==(const std::pair<csdb::Pool, cs::PublicKey>& lhs, uint64_t rhs) {
  return lhs.first.sequence() == rhs;
}
}  // namespace std

namespace cs {

void DefaultStateBehavior::onRoundEnd(SolverContext& /*context*/, bool /*is_bigbang*/) {
}

Result DefaultStateBehavior::onRoundTable(SolverContext& /*context*/, const size_t round) {
  if (Consensus::Log) {
    LOG_NOTICE(name() << ": <-- round table [" << round << "]");
  }
  return Result::Finish;
}

Result DefaultStateBehavior::onBlock(SolverContext& /*context*/, csdb::Pool& /*block*/, const cs::PublicKey& /*sender*/) {
  cswarning() << name() << ": currently block should not handle by state";
  return Result::Ignore;
}

Result DefaultStateBehavior::onHash(SolverContext& /*context*/, const csdb::PoolHash& /*pool_hash*/,
                                    const cs::PublicKey& /*sender*/) {
  if (Consensus::Log) {
    LOG_DEBUG(name() << ": block hash ignored in this state");
  }
  return Result::Ignore;
}

Result DefaultStateBehavior::onTransaction(SolverContext& /*context*/, const csdb::Transaction& /*trans*/) {
  if (Consensus::Log) {
    LOG_DEBUG(name() << ": nothing to do with transaction in this state");
  }
  return Result::Ignore;
}

Result DefaultStateBehavior::onSyncTransactions(SolverContext& /*context*/, cs::RoundNumber /*round*/) {
  if (Consensus::Log) {
    LOG_DEBUG(name() << ": nothing to do with transactions packet in this state");
  }
  return Result::Ignore;
}

Result DefaultStateBehavior::onStage1(SolverContext& /*context*/, const cs::StageOne& /*stage*/) {
  if (Consensus::Log) {
    LOG_DEBUG(name() << ": stage-1 ignored in this state");
  }
  return Result::Ignore;
}

Result DefaultStateBehavior::onStage2(SolverContext& /*context*/, const cs::StageTwo& /*stage*/) {
  if (Consensus::Log) {
    LOG_DEBUG(name() << ": stage-2 ignored in this state");
  }
  return Result::Ignore;
}

Result DefaultStateBehavior::onStage3(SolverContext& /*context*/, const cs::StageThree& /*stage*/) {
  if (Consensus::Log) {
    LOG_DEBUG(name() << ": stage-3 ignored in this state");
  }
  return Result::Ignore;
}

}  // namespace slv2