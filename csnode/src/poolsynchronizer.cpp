#include "poolsynchronizer.hpp"

#include <lib/system/logger.hpp>
#include <lib/system/utils.hpp>
#include <lib/system/progressbar.hpp>

#include <net/transport.hpp>

cs::PoolSynchronizer::PoolSynchronizer(Transport* transport, BlockChain* blockChain) :
    m_transport(transport), /// TODO Fix me. Think about how to do without it
    m_blockChain(blockChain)
{
    m_neededSequences.reserve(m_maxBlockCount);
    m_neighbours.reserve(m_transport->getMaxNeighbours());

    refreshNeighbours();
}

void cs::PoolSynchronizer::processingSync(const cs::RoundNumber roundNum) {
    if (m_transport->getNeighboursCount() == 0) {
        cslog() << "POOL SYNCHRONIZER> Cannot start sync (no neighbours). Needed sequence: " << roundNum;
        return;
    }

    if (!m_isSyncroStarted) {
        if (roundNum >= m_blockChain->getLastWrittenSequence() + s_roundDifferent) {
            cslog() << "POOL SYNCHRONIZER> Processing Pools Sync Start. Needed sequence: " << roundNum;
            m_isSyncroStarted = true;
            m_roundToSync = roundNum;

            sendBlockRequest();
        }
    }
    else {
        checkActivity(true);
    }
}

void cs::PoolSynchronizer::getBlockReply(cs::PoolsBlock&& poolsBlock, uint32_t packCounter) {
    cslog() << "POOL SYNCHRONIZER> Get Block Reply <<<<<<< from: " << poolsBlock.front().sequence() << ", to: " << poolsBlock.back().sequence() << ",   packCounter: " << packCounter;

    /// TODO Fix numeric cast from RoundNum to csdb::Pool::sequence_t
    csdb::Pool::sequence_t lastWrittenSequence = cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());

    if (poolsBlock.back().sequence() > lastWrittenSequence) {
        checkNeighbourSequence(poolsBlock.front().sequence());

        for (auto& pool : poolsBlock) {
            const auto sequence = pool.sequence();
            auto it = m_requestedSequences.find(sequence);
            if (it != m_requestedSequences.end()) {
                m_requestedSequences.erase(it);
            }

            if (lastWrittenSequence > sequence) {
                continue;
            }

            if (m_blockChain->getGlobalSequence() < sequence) {
                m_blockChain->setGlobalSequence(cs::numeric_cast<uint32_t>(sequence));
            }

            if (sequence == lastWrittenSequence + 1) {
                cslog() << "POOL SYNCHRONIZER> Block Sequence is Ok " << sequence;

                m_blockChain->onBlockReceived(pool);
                lastWrittenSequence = cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());
            }
            else if (sequence > lastWrittenSequence) {
                addToTemporaryStorage(pool);
            }
        }

        lastWrittenSequence = processingTemporaryStorage();

        csdebug() << "POOL SYNCHRONIZER> Last written sequence on blockchain: " << lastWrittenSequence << ", needed seq: " << m_roundToSync;
        showSyncronizationProgress(lastWrittenSequence);
    }

    // Decreases, soon as a response is received for another requested block.
    const bool alreadyRequest = checkActivity(false);

    /// or m_roundToSync > lastWrittenSequence
    if (m_roundToSync != cs::numeric_cast<cs::RoundNumber>(lastWrittenSequence)) {
        if (!alreadyRequest) {
            sendBlockRequest();
        }
    }
    else {
        m_isSyncroStarted = false;
        m_roundToSync = 0;
        m_requestedSequences.clear();
        m_temporaryStorage.clear();
        m_neededSequences.clear();
        m_neighbours.clear();

        cslog() << "POOL SYNCHRONIZER> !!! !!! !!! !!! SYNCHRO FINISHED !!! !!! !!! !!!";
        emit synchroFinished();
    }
}

void cs::PoolSynchronizer::sendBlockRequest() {
    refreshNeighbours();

    if (m_neighbours.empty()) {
        csdebug() << "POOL SYNCHRONIZER> No more free requestees";
        return;
    }

    if (!getNeededSequences()) {
        csdebug() << "POOL SYNCHRONIZER> >>> All sequences already requested";
        return;
    }

    // sequence = 0 if already requested
    checkNeighbourSequence(m_neededSequences.front());

    for (auto& neighbour : m_neighbours) {
        if (neighbour.sequence == 0) {
            neighbour.sequence = m_neededSequences.front();

            sendBlock(neighbour.connection, m_neededSequences);

            if (!getNeededSequences()) {
                csdebug() << "POOL SYNCHRONIZER> !!! All sequences already requested";
                break;
            }
        }
    }
}

bool cs::PoolSynchronizer::isSyncroStarted() const {
    return m_isSyncroStarted;
}

//
// Service
//

void cs::PoolSynchronizer::showSyncronizationProgress(const csdb::Pool::sequence_t lastWrittenSequence) {
    const csdb::Pool::sequence_t globalSequence = cs::numeric_cast<csdb::Pool::sequence_t>(m_roundToSync);

    if (!globalSequence) {
        return;
    }

    const auto last = float(lastWrittenSequence + m_temporaryStorage.size());
    const auto global = float(globalSequence);
    const float maxValue = 100.0f;
    const uint32_t syncStatus = cs::numeric_cast<uint32_t>((last / global) * maxValue);

    if (syncStatus <= maxValue) {
        ProgressBar bar;
        cslog() << "SYNC: " << bar.string(syncStatus);
    }
}

bool cs::PoolSynchronizer::checkActivity(bool isRound) {
    const std::string flag = (isRound ? "round" : "syncro");
    csdebug() << "POOL SYNCHRONIZER> Check activity " << flag;

    bool isNeedRequest = false;

    for (auto& [sequence, timeWaiting] : m_requestedSequences) {
        if (isRound) {
            --(timeWaiting.roundCount);
            csdebug() << "POOL SYNCHRONIZER> seq: " << sequence << ", Activity round: " << timeWaiting.roundCount;
            if (!isNeedRequest && timeWaiting.roundCount <= 0) {
                isNeedRequest = true;
            }
        }
        else {
            --(timeWaiting.replyBlockCount);
            csdebug() << "POOL SYNCHRONIZER> seq: " << sequence << ", Activity reply: " << timeWaiting.replyBlockCount;
            if (!isNeedRequest && timeWaiting.replyBlockCount <= 0) {
                isNeedRequest = true;
            }
        }
    }

    if (isNeedRequest) {
        sendBlockRequest();
    }

    return isNeedRequest;
}

void cs::PoolSynchronizer::sendBlock(const ConnectionPtr& target, const PoolsRequestedSequences& sequences) {
    csdebug() << "POOL SYNCHRONIZER> Sending block request : from nbr: " << target->getOut() << ", id: " << target->id;

    uint32_t packCounter = 0;

    for (const auto& sequence : sequences) {
        if (!m_requestedSequences.count(sequence)) {
            m_requestedSequences.emplace(std::make_pair(sequence, WaitinTimeReply(m_maxWaitingTimeRound, m_maxWaitingTimeReply)));
        }
        ++(m_requestedSequences.at(sequence).packCounter);
        packCounter = m_requestedSequences.at(sequence).packCounter;
    }

    cslog() << "POOL SYNCHRONIZER> Sending block request >>>> needed seq: " << m_blockChain->getLastWrittenSequence() + 1
        << ", requested block sequences from: " << sequences.front() << ", to: " << sequences.back()
        << ",   packCounter: " << packCounter;

    csdebug() << "POOL SYNCHRONIZER> Requested sequences from: " << m_requestedSequences.begin()->first << ", to: " << m_requestedSequences.rbegin()->first;

    emit sendRequest(target, sequences, packCounter);
}

void cs::PoolSynchronizer::addToTemporaryStorage(const csdb::Pool& pool) {
    const auto sequence = pool.sequence();
    const auto transactionsCount = pool.transactions_count();

    if (!m_temporaryStorage.count(sequence)) {
        m_temporaryStorage.emplace(std::make_pair(sequence, pool));
        csdebug() << "POOL SYNCHRONIZER> Store received block: " << sequence << ", transactions: " << transactionsCount;
    }
}

csdb::Pool::sequence_t cs::PoolSynchronizer::processingTemporaryStorage() {
    csdb::Pool::sequence_t lastSequence = cs::numeric_cast<csdb::Pool::sequence_t>(m_blockChain->getLastWrittenSequence());

    if (m_temporaryStorage.empty()) {
        csdebug() << "POOL SYNCHRONIZER> Temporary storage is empty";
        return lastSequence;
    }

    csdb::Pool::sequence_t neededSequence = lastSequence;
    const auto storageSize = m_temporaryStorage.size();

    for (std::size_t i = 0; i < storageSize; ++i) {
        ++neededSequence;
        csdebug() << "POOL SYNCHRONIZER> Processing TemporaryStorage: needed sequence: " << neededSequence;

        auto it = m_temporaryStorage.find(neededSequence);

        if (it != m_temporaryStorage.end()) {
            csdebug() << "POOL SYNCHRONIZER> Temporary storage contains sequence: " << neededSequence << ", with transactions: " << it->second.transactions_count();
            m_blockChain->onBlockReceived(it->second);
            m_temporaryStorage.erase(it);
            lastSequence = neededSequence;
        }
        else {
            csdebug() << "POOL SYNCHRONIZER> Processing TemporaryStorage: needed sequence: " << neededSequence << " not contained in storage";
            break;
        }
    }

    return lastSequence;
}

bool cs::PoolSynchronizer::getNeededSequences() {
    uint32_t lastSequence = 0;
    bool isFromStorage = false;
    auto firstSequenceIt = std::find_if(m_requestedSequences.begin(), m_requestedSequences.end(), [](const auto& pair) {
        return (pair.second.roundCount <= 0 || pair.second.replyBlockCount <= 0);
    });

    if (!m_temporaryStorage.empty()) {
        csdebug() << "POOL SYNCHRONIZER> Temporary Storage begin: " << m_temporaryStorage.begin()->first << ", end: " << m_temporaryStorage.rbegin()->first;
    }
    if (!m_requestedSequences.empty()) {
        csdebug() << "POOL SYNCHRONIZER> Request Storage begin: " << m_requestedSequences.begin()->first << ", end: " << m_requestedSequences.rbegin()->first;
    }


    // if storage requested sequences is impty
    if (m_requestedSequences.empty()) {
        csdebug() << "POOL SYNCHRONIZER> Get sequences: from blockchain";
        lastSequence = m_blockChain->getLastWrittenSequence();
    }
    else if (firstSequenceIt != m_requestedSequences.end()) {
        // if maxWaitingTimeReply <= 0
        csdebug() << "POOL SYNCHRONIZER> Get sequences: from requestedSequences begin";
        lastSequence = cs::numeric_cast<uint32_t>(firstSequenceIt->first);
        isFromStorage = true;
    }
    else {
        const uint32_t lastSeqFromRequested = cs::numeric_cast<uint32_t>(m_requestedSequences.rbegin()->first);
        const uint32_t lastSeqFromStorage = m_temporaryStorage.empty() ? 0 : cs::numeric_cast<uint32_t>(m_temporaryStorage.rbegin()->first);
        csdebug() << "POOL SYNCHRONIZER> Get sequences: from requested storage: " << lastSeqFromRequested;
        csdebug() << "POOL SYNCHRONIZER> Get sequences: from temp storage: " << lastSeqFromStorage;
        lastSequence = std::max(lastSeqFromRequested, lastSeqFromStorage);
    }
    csdebug() << "POOL SYNCHRONIZER> Get sequences: lastSequence: " << lastSequence;

    m_neededSequences.clear();

    for (std::size_t i = 0; i < m_maxBlockCount; ++i) {
        if (!isFromStorage) {
            ++lastSequence;
        }

        // max sequence
        if (lastSequence > m_roundToSync) {
            break;
        }

        m_neededSequences.push_back(lastSequence);

        if (isFromStorage) {
            if (firstSequenceIt->second.roundCount <= 0 || firstSequenceIt->second.replyBlockCount <= 0) {
                firstSequenceIt->second.roundCount = m_maxWaitingTimeRound; // reset maxWaitingTimeReply
                firstSequenceIt->second.replyBlockCount = m_maxWaitingTimeReply; // reset maxWaitingTimeReply
            }
            ++firstSequenceIt; // next sequence
            if (firstSequenceIt == m_requestedSequences.end()) {
                break;
            }
            lastSequence = cs::numeric_cast<uint32_t>(firstSequenceIt->first);
        }
    }

    if (!m_neededSequences.empty()) {
        csdebug() << "POOL SYNCHRONIZER> Get sequences: neededSequences: from: " << m_neededSequences.front() << ", to: " << m_neededSequences.back();
    }

    return !m_neededSequences.empty();
}

void cs::PoolSynchronizer::checkNeighbourSequence(const csdb::Pool::sequence_t sequence) {
    for (auto& neighbour : m_neighbours) {
        if (neighbour.sequence == sequence) {
            neighbour.sequence = 0;
        }
    }
}

void cs::PoolSynchronizer::refreshNeighbours() {
  const uint32_t neighboursCount = m_transport->getNeighboursCount();

  if (neighboursCount == m_neighbours.size() + 1) { // + Signal
      return;
  }

  m_neighbours.clear();

  for (std::size_t i = 0; i != neighboursCount; ++i) {
      ConnectionPtr target = m_transport->getNeighbourByNumber(i);
      if (target && !target->isSignal) {
          m_neighbours.emplace_back(NeighboursSetElemet(0, target));
      }
  }

  csdebug() << "POOL SYNCHRONIZER> Neighbours count is: " << m_neighbours.size();
}
