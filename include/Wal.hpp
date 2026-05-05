#pragma once

#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>

enum class WalOp : std::uint8_t {
    Set = 1,
    Del = 2,
};

struct WalRecord {
    WalOp op{WalOp::Set};
    std::string key;
    std::string value;
    std::int64_t expiry_unix_ms{-1};
};

class WalWriter {
public:
    explicit WalWriter(const std::string& path, bool truncate = false);

    void append_set(const std::string& key, const std::string& value, std::int64_t expiry_unix_ms);
    void append_del(const std::string& key);

private:
    void append(const WalRecord& record);

    std::ofstream out_;
};

class WalReader {
public:
    explicit WalReader(const std::string& path);

    std::optional<WalRecord> next();

private:
    std::ifstream in_;
};

std::int64_t to_unix_ms(std::chrono::system_clock::time_point time);
std::chrono::system_clock::time_point from_unix_ms(std::int64_t unix_ms);
