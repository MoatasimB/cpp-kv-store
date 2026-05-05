#pragma once

#include <chrono>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <atomic>

struct ValueEntry {
    std::string value;
    bool has_expiry{false};
    std::chrono::system_clock::time_point expiry_time;
};

class KVStore {
public:
    explicit KVStore(const std::string& snapshot_path, std::string wal_path = "");
    ~KVStore();

    void set(const std::string& key, const std::string& value);
    std::optional<std::string> get(const std::string& key);
    bool del(const std::string& key);
    bool expire(const std::string& key, int seconds);
    std::optional<long long> ttl(const std::string& key);
    std::size_t size() const;
    void save_snapshot();
    void load_snapshot();
    void compact();

private:
    void set_internal(const std::string& key, const std::string& value,
                      std::optional<std::chrono::system_clock::time_point> expiry,
                      bool persist);
    bool del_internal(const std::string& key, bool persist);
    void cleanup_loop();
    void cleanup_expired_locked();
    void replay_wal();
    void append_wal_set(const std::string& key, const ValueEntry& entry);
    void append_wal_del(const std::string& key);
    bool is_expired(const ValueEntry& entry) const;

    std::unordered_map<std::string, ValueEntry> store_;
    mutable std::shared_mutex mutex_;
    std::string snapshot_path_;
    std::string wal_path_;
    std::atomic<bool> running_{true};
    std::thread cleanup_thread_;
};
