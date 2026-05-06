#include "KVStore.hpp"
#include "Wal.hpp"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <utility>

KVStore::KVStore(const std::string& snapshot_path, std::string wal_path, std::size_t max_keys)
    : snapshot_path_(snapshot_path),
      wal_path_(wal_path.empty() ? snapshot_path + ".wal" : std::move(wal_path)),
      max_keys_(max_keys) {
    load_snapshot();
    replay_wal();
    cleanup_thread_ = std::thread(&KVStore::cleanup_loop, this);
}

KVStore::~KVStore() {
    running_ = false;
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
    save_snapshot();
}

void KVStore::set(const std::string& key, const std::string& value) {
    set_internal(key, value, std::nullopt, true);
}

void KVStore::set_internal(const std::string& key, const std::string& value,
                           std::optional<std::chrono::system_clock::time_point> expiry,
                           bool persist) {
    std::unique_lock lock(mutex_);
    ValueEntry entry;
    entry.value = value;
    if (expiry.has_value()) {
        entry.has_expiry = true;
        entry.expiry_time = *expiry;
    }
    if (persist) {
        append_wal_set(key, entry);
    }
    store_[key] = entry;
    touch_lru_locked(key);
    evict_if_needed_locked(persist);
}

std::optional<std::string> KVStore::get(const std::string& key) {
    std::unique_lock lock(mutex_);
    auto it = store_.find(key);
    if (it == store_.end()) {
        return std::nullopt;
    }
    if (is_expired(it->second)) {
        append_wal_del(key);
        remove_lru_locked(key);
        store_.erase(it);
        return std::nullopt;
    }
    touch_lru_locked(key);
    return it->second.value;
}

bool KVStore::del(const std::string& key) {
    return del_internal(key, true);
}

bool KVStore::del_internal(const std::string& key, bool persist) {
    std::unique_lock lock(mutex_);
    const bool removed = store_.erase(key) > 0;
    if (removed && persist) {
        append_wal_del(key);
    }
    if (removed) {
        remove_lru_locked(key);
    }
    return removed;
}

bool KVStore::expire(const std::string& key, int seconds) {
    if (seconds < 0) {
        return false;
    }
    std::unique_lock lock(mutex_);
    auto it = store_.find(key);
    if (it == store_.end()) {
        return false;
    }
    if (is_expired(it->second)) {
        append_wal_del(key);
        remove_lru_locked(key);
        store_.erase(it);
        return false;
    }
    it->second.has_expiry = true;
    it->second.expiry_time = std::chrono::system_clock::now() + std::chrono::seconds(seconds);
    append_wal_set(key, it->second);
    touch_lru_locked(key);
    return true;
}

std::optional<long long> KVStore::ttl(const std::string& key) {
    auto value = get(key);
    if (!value.has_value()) {
        return std::nullopt;
    }

    std::shared_lock lock(mutex_);
    auto it = store_.find(key);
    if (it == store_.end()) {
        return std::nullopt;
    }
    if (!it->second.has_expiry) {
        return -1;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
        it->second.expiry_time - std::chrono::system_clock::now());
    return std::max<long long>(0, remaining.count());
}

std::size_t KVStore::size() const {
    std::shared_lock lock(mutex_);
    return store_.size();
}

void KVStore::cleanup_expired_locked() {
    for (auto it = store_.begin(); it != store_.end();) {
        if (is_expired(it->second)) {
            append_wal_del(it->first);
            remove_lru_locked(it->first);
            it = store_.erase(it);
        } else {
            ++it;
        }
    }
}

void KVStore::cleanup_loop() {
    while (running_) {
        {
            std::unique_lock lock(mutex_);
            cleanup_expired_locked();
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void KVStore::save_snapshot() {
    std::shared_lock lock(mutex_);
    const std::string temp_path = snapshot_path_ + ".tmp";
    WalWriter writer(temp_path, true);
    const auto now = std::chrono::system_clock::now();
    for (const auto& [key, entry] : store_) {
        if (entry.has_expiry && now >= entry.expiry_time) {
            continue;
        }
        writer.append_set(key, entry.value, entry.has_expiry ? to_unix_ms(entry.expiry_time) : -1);
    }
    lock.unlock();
    std::remove(snapshot_path_.c_str());
    std::rename(temp_path.c_str(), snapshot_path_.c_str());
}

void KVStore::compact() {
    save_snapshot();
    WalWriter writer(wal_path_, true);
}

void KVStore::load_snapshot() {
    WalReader reader(snapshot_path_);
    while (auto record = reader.next()) {
        if (record->op == WalOp::Set) {
            std::optional<std::chrono::system_clock::time_point> expiry;
            if (record->expiry_unix_ms >= 0) {
                expiry = from_unix_ms(record->expiry_unix_ms);
                if (*expiry <= std::chrono::system_clock::now()) {
                    continue;
                }
            }
            set_internal(record->key, record->value, expiry, false);
        } else if (record->op == WalOp::Del) {
            del_internal(record->key, false);
        }
    }
}

void KVStore::replay_wal() {
    WalReader reader(wal_path_);
    while (auto record = reader.next()) {
        if (record->op == WalOp::Set) {
            std::optional<std::chrono::system_clock::time_point> expiry;
            if (record->expiry_unix_ms >= 0) {
                expiry = from_unix_ms(record->expiry_unix_ms);
                if (*expiry <= std::chrono::system_clock::now()) {
                    del_internal(record->key, false);
                    continue;
                }
            }
            set_internal(record->key, record->value, expiry, false);
        } else if (record->op == WalOp::Del) {
            del_internal(record->key, false);
        }
    }
}

void KVStore::append_wal_set(const std::string& key, const ValueEntry& entry) {
    WalWriter writer(wal_path_);
    writer.append_set(key, entry.value, entry.has_expiry ? to_unix_ms(entry.expiry_time) : -1);
}

void KVStore::append_wal_del(const std::string& key) {
    WalWriter writer(wal_path_);
    writer.append_del(key);
}

bool KVStore::is_expired(const ValueEntry& entry) const {
    return entry.has_expiry && std::chrono::system_clock::now() >= entry.expiry_time;
}

void KVStore::touch_lru_locked(const std::string& key) {
    remove_lru_locked(key);
    lru_order_.push_front(key);
    lru_index_[key] = lru_order_.begin();
}

void KVStore::remove_lru_locked(const std::string& key) {
    auto it = lru_index_.find(key);
    if (it == lru_index_.end()) {
        return;
    }
    lru_order_.erase(it->second);
    lru_index_.erase(it);
}

void KVStore::evict_if_needed_locked(bool persist) {
    while (max_keys_ > 0 && store_.size() > max_keys_ && !lru_order_.empty()) {
        const std::string victim = lru_order_.back();
        lru_order_.pop_back();
        lru_index_.erase(victim);
        if (store_.erase(victim) > 0 && persist) {
            append_wal_del(victim);
        }
    }
}
