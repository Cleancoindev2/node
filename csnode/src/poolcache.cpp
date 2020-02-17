#include <poolcache.hpp>

#include <csdb/pool.hpp>

static const std::string dbPath = "/poolcachedb";

cs::PoolCache::PoolCache(const std::string& path)
: db_(path + dbPath) {
    initialization();
}

cs::PoolCache::~PoolCache() {
    db_.close();
}

void cs::PoolCache::insert(const csdb::Pool& pool, PoolStoreType type) {
    insert(pool.sequence(), pool.to_binary(), type);
}

void cs::PoolCache::insert(cs::Sequence sequence, const cs::Bytes& bytes, cs::PoolStoreType type) {
    type_ = type;
    db_.insert(sequence, bytes);
}

bool cs::PoolCache::remove(cs::Sequence sequence) {
    return db_.remove(sequence);
}

void cs::PoolCache::remove(cs::Sequence from, cs::Sequence to) {
    if (from > to) {
        return;
    }

    for (; from <= to; ++from) {
        remove(from);
    }
}

bool cs::PoolCache::contains(cs::Sequence sequence) const {
    return sequences_.find(sequence) != sequences_.end();
}

bool cs::PoolCache::isEmpty() const {
    return sequences_.empty();
}

cs::Sequence cs::PoolCache::minSequence() const {
    return (sequences_.begin())->first;
}

cs::Sequence cs::PoolCache::maxSequence() const {
    return std::prev(sequences_.end())->first;
}

std::pair<csdb::Pool, cs::PoolStoreType> cs::PoolCache::value(cs::Sequence sequence) const {
    auto bytes = db_.value<cs::Bytes>(sequence);
    return std::make_pair(csdb::Pool::from_binary(std::move(bytes)), cachedType(sequence));
}

std::pair<csdb::Pool, cs::PoolStoreType> cs::PoolCache::pop(cs::Sequence sequence) {
    auto [pool, type] = value(sequence);
    db_.remove(sequence);

    return std::make_pair(std::move(pool), type);
}

size_t cs::PoolCache::size() const {
    return sequences_.size();
}

size_t cs::PoolCache::sizeSynced() const {
    return syncedPoolSize_;
}

size_t cs::PoolCache::sizeCreated() const {
    return size() - sizeSynced();
}

void cs::PoolCache::clear() {
    if (isEmpty()) {
        return;
    }

    auto min = minSequence();
    auto max = maxSequence();

    for (; min <= max; ++min) {
        remove(min);
    }
}

void cs::PoolCache::onInserted(const char* data, size_t size) {
    if (type_ == cs::PoolStoreType::Synced) {
        ++syncedPoolSize_;
    }

    sequences_.emplace(cs::Lmdb::convert<cs::Sequence>(data, size), type_);
}

void cs::PoolCache::onRemoved(const char* data, size_t size) {
    const auto iter = sequences_.find(cs::Lmdb::convert<cs::Sequence>(data, size));

    if (iter != sequences_.end()) {
        if (iter->second == cs::PoolStoreType::Synced) {
            --syncedPoolSize_;
        }

        sequences_.erase(iter);
    }
}

void cs::PoolCache::onFailed(const cs::LmdbException& exception) {
    cswarning() << csfunc() << ", pool caches database exception: " << exception.what();
}

void cs::PoolCache::initialization() {
    cs::Connector::connect(&db_.commited, this, &cs::PoolCache::onInserted);
    cs::Connector::connect(&db_.removed, this, &cs::PoolCache::onRemoved);
    cs::Connector::connect(&db_.failed, this, &cs::PoolCache::onFailed);

    db_.setMapSize(cs::Lmdb::Default1GbMapSize);
    db_.open();
}

cs::PoolStoreType cs::PoolCache::cachedType(cs::Sequence sequence) const {
    return sequences_.find(sequence)->second;
}
