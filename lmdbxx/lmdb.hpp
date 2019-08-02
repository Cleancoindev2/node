#ifndef LMDBXX_HPP
#define LMDBXX_HPP

#include <cassert>
#include <charconv>

#include <lmdbexception.hpp>

#include <lib/system/signals.hpp>
#include <lib/system/reflection.hpp>

namespace cs {
using FlushSignal = cs::Signal<void()>;
using CommitSignal = cs::Signal<void(const char* data, size_t size)>;
using RemoveSignal = cs::Signal<void(const char* data, size_t size)>;
using FailureSignal = cs::Signal<void(const LmdbException& error)>;

// lmdbxx RAII wrapper, not thread safe by default
class Lmdb {
public:
    enum Options : unsigned int {
        DefaultMapSize = 1UL * 1024UL * 1024UL * 1024UL
    };

    explicit Lmdb(const std::string& path, const unsigned int flags = lmdb::env::default_flags);
    ~Lmdb() noexcept;

    /// database settings

    // opens lmdb
    void open(const unsigned int flags = lmdb::env::default_flags,
              const lmdb::mode mode = lmdb::env::default_mode) {
        try {
            env_.open(path_.c_str(), flags, mode);
            isOpened_ = true;
        }
        catch(const lmdb::error& error) {
            raise(error);
        }
    }

    void close() {
        env_.close();
        isOpened_ = false;
    }

    // returns database open status
    bool isOpened() const {
        return isOpened_;
    }

    void setFlags(const unsigned int flags, const bool onoff = true) {
        try {
            env_.set_flags(flags, onoff);
        }
        catch(const lmdb::error& error) {
            raise(error);
        }
    }

    void setMaxReaders(std::size_t count) {
        try {
            env_.set_max_readers(static_cast<unsigned int>(count));
        }
        catch(const lmdb::error& error) {
            raise(error);
        }
    }

    void setMaxDbs(std::size_t count) {
        try {
            env_.set_max_dbs(static_cast<MDB_dbi>(count));
        }
        catch(const lmdb::error& error) {
            raise(error);
        }
    }

    // sets mapped size in bytes
    void setMapSize(std::size_t size) {
        try {
            env_.set_mapsize(size);
        }
        catch(const lmdb::error& error) {
            raise(error);
        }
    }

    // flushes data to drive in sync mode
    void flush() {
        flushImpl(true);
    }

    // flushes data to drive in async mode
    void flushAsync() {
        flushImpl(false);
    }

    // returns database elements count,
    // name - table name at current path, nullptr if only one table exist
    size_t size(const char* name = nullptr) const {
        try {
            auto transaction = lmdb::txn::begin(env_, nullptr, MDB_RDONLY);
            auto dbi = lmdb::dbi::open(transaction, name);

            return dbi.size(transaction);
        }
        catch(const lmdb::error& error) {
            raise(error);
        }

        return 0;
    }

    /// transactions

    // inserts pair of key/value to database as byte stream,
    // name - table name at current path, nullptr if only one table exist,
    // with default flags rewrites value if key exists at db
    void insert(const char* keyData, std::size_t keySize, const char* valueData, std::size_t valueSize,
                const char* name = nullptr,
                const unsigned int flags = lmdb::dbi::default_put_flags) {
        try {
            auto transaction = lmdb::txn::begin(env_);
            auto dbi = lmdb::dbi::open(transaction, name);

            lmdb::val key(reinterpret_cast<const void*>(keyData), keySize);
            lmdb::val value(reinterpret_cast<const void*>(valueData), valueSize);

            dbi.put(transaction, key, value, flags);
            transaction.commit();

            emit commited(keyData, keySize);
        }
        catch(const lmdb::error& error) {
            raise(error);
        }
    }

    // inserts any key or value with data/size methods
    template<typename Key, typename Value>
    void insert(const Key& key, const Value& value,
                const char* name = nullptr,
                const unsigned int flags = lmdb::dbi::default_flags) {
        decltype(auto) k = cast(key);
        decltype(auto) v = cast(value);

        insert(reinterpret_cast<const char*>(k.data()), k.size(),
               reinterpret_cast<const char*>(v.data()), v.size(),
               name, flags);
    }

    // removes key/value pair by key argument as byte stream
    // name - table name at current path, nullptr if only one table exist
    bool remove(const char* data, size_t size, const char* name = nullptr,
                const unsigned int flags = lmdb::dbi::default_flags) {
        try {
            auto transaction = lmdb::txn::begin(env_, nullptr);
            auto dbi = lmdb::dbi::open(transaction, name);
            auto cursor = lmdb::cursor::open(transaction, dbi);

            lmdb::val key(reinterpret_cast<const void*>(data), size);
            const auto result = cursor.get(key, nullptr, MDB_SET);

            if (result) {
                lmdb::cursor_del(cursor.handle(), flags);
                transaction.commit();

                emit removed(data, size);
            }

            return result;
        }
        catch(const lmdb::error& error) {
            raise(error);
        }

        return false;
    }

    // removes key/value pair by key as data/size method entity
    template<typename Key>
    bool remove(const Key& key, const char* name = nullptr, const unsigned int flags = lmdb::dbi::default_flags) {
        decltype(auto) k = cast(key);
        return remove(reinterpret_cast<const char*>(k.data()), k.size(), name, flags);
    }

    // returns key status at database,
    // name - table name at current path
    bool isKeyExists(const char* data, size_t size, const char* name = nullptr) const {
        try {
            auto transaction = lmdb::txn::begin(env_, nullptr, MDB_RDONLY);
            auto dbi = lmdb::dbi::open(transaction, name);
            auto cursor = lmdb::cursor::open(transaction, dbi);

            lmdb::val key(reinterpret_cast<const void*>(data), size);
            return cursor.get(key, nullptr, MDB_SET);
        }
        catch(const lmdb::error& error) {
            raise(error);
        }

        return false;
    }

    template<typename Key>
    bool isKeyExists(const Key& key, const char* name = nullptr) const {
        decltype(auto) k = cast(key);
        return isKeyExists(reinterpret_cast<const char*>(k.data()), k.size(), name);
    }

    // returns value by key, casts it to template argument
    template<typename T>
    T value(const char* data, size_t size, const char* name = nullptr) const {
        try {
            auto transaction = lmdb::txn::begin(env_, nullptr, MDB_RDONLY);
            auto dbi = lmdb::dbi::open(transaction, name);
            auto cursor = lmdb::cursor::open(transaction, dbi);

            lmdb::val key(reinterpret_cast<const void*>(data), size);
            lmdb::val value;

            const auto result = cursor.get(key, value, MDB_SET);

            if (result) {
                return createResult<T>(value);
            }
        }
        catch(const lmdb::error& error) {
            raise(error);
        }

        return T{};
    }

    // returns and cast to any result with interator consturctor,
    // any key with data/size methods
    template<typename T, typename Key>
    T value(const Key& key) const {
        decltype(auto) k = cast(key);
        return value<T>(reinterpret_cast<const char*>(k.data()), k.size());
    }

protected:
    void flushImpl(bool force) {
        try {
            env_.sync(force);
            emit flushed();
        }
        catch(const lmdb::error& error) {
            raise(error);
        }
    }

    void raise(const lmdb::error& error) const {
        emit failed(LmdbException(error));
    }

    template<typename T, typename = std::enable_if_t<std::is_integral_v<T> || std::is_floating_point_v<T>>>
    auto cast(const T& value) const {
        static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>, "Value must be integral or floating point to cast");

        std::array<char, sizeof(T)> bytes{};
        std::to_chars(bytes.data(), bytes.data() + bytes.size(), value);

        return bytes;
    }

    template<typename T, typename = std::enable_if_t<!std::is_integral_v<T> && !std::is_floating_point_v<T>>>
    const T& cast(const T& value) const {
        return value;
    }

    template<typename T>
    T createResult(const lmdb::val& value) const {
        if constexpr (IsArray<T>::value) {
            T array;
            assert(array.size() == value.size());
            std::copy(value.data(), value.data() + value.size(), array.data());

            return array;
        }
        else if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>) {
            T result = 0;
            std::from_chars(value.data(), value.data() + value.size(), result);

            return result;
        }
        else {
            return T(value.data(), value.data() + value.size());
        }
    }

private:
    lmdb::env env_;
    std::string path_;
    bool isOpened_ = false;

public signals:

    // generates when data is written to drive
    FlushSignal flushed;

    // generates when lmdbxx generates an exception
    FailureSignal failed;

    // generates when database data key is commited
    CommitSignal commited;

    // generates when database data key is removed
    RemoveSignal removed;
};
}

#endif // LMDBXX_HPP
