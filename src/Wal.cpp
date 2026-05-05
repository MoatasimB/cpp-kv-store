#include "Wal.hpp"

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
constexpr std::uint32_t kMagic = 0x4b565731;  // "KV1"

struct Header {
    std::uint32_t magic{kMagic};
    std::uint8_t op{0};
    std::uint32_t key_len{0};
    std::uint32_t value_len{0};
    std::int64_t expiry_unix_ms{-1};
    std::uint64_t checksum{0};
};

std::uint64_t fnv1a(const void* data, std::size_t size, std::uint64_t hash = 1469598103934665603ULL) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint64_t checksum_for(const Header& header, const std::string& key, const std::string& value) {
    Header copy = header;
    copy.checksum = 0;
    auto hash = fnv1a(&copy, sizeof(copy));
    hash = fnv1a(key.data(), key.size(), hash);
    return fnv1a(value.data(), value.size(), hash);
}
}  // namespace

WalWriter::WalWriter(const std::string& path, bool truncate)
    : out_(path, std::ios::binary | (truncate ? std::ios::trunc : std::ios::app)) {
    if (!out_) {
        throw std::runtime_error("failed to open WAL file: " + path);
    }
}

void WalWriter::append_set(const std::string& key, const std::string& value, std::int64_t expiry_unix_ms) {
    append(WalRecord{WalOp::Set, key, value, expiry_unix_ms});
}

void WalWriter::append_del(const std::string& key) {
    append(WalRecord{WalOp::Del, key, "", -1});
}

void WalWriter::append(const WalRecord& record) {
    Header header;
    header.op = static_cast<std::uint8_t>(record.op);
    header.key_len = static_cast<std::uint32_t>(record.key.size());
    header.value_len = static_cast<std::uint32_t>(record.value.size());
    header.expiry_unix_ms = record.expiry_unix_ms;
    header.checksum = checksum_for(header, record.key, record.value);

    out_.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out_.write(record.key.data(), static_cast<std::streamsize>(record.key.size()));
    out_.write(record.value.data(), static_cast<std::streamsize>(record.value.size()));
    out_.flush();
    if (!out_) {
        throw std::runtime_error("failed to write WAL record");
    }
}

WalReader::WalReader(const std::string& path) : in_(path, std::ios::binary) {}

std::optional<WalRecord> WalReader::next() {
    if (!in_) {
        return std::nullopt;
    }

    Header header;
    in_.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (in_.gcount() == 0) {
        return std::nullopt;
    }
    if (in_.gcount() != static_cast<std::streamsize>(sizeof(header))) {
        return std::nullopt;
    }
    if (header.magic != kMagic ||
        (header.op != static_cast<std::uint8_t>(WalOp::Set) &&
         header.op != static_cast<std::uint8_t>(WalOp::Del))) {
        return std::nullopt;
    }

    std::string key(header.key_len, '\0');
    std::string value(header.value_len, '\0');
    in_.read(key.data(), static_cast<std::streamsize>(key.size()));
    if (in_.gcount() != static_cast<std::streamsize>(key.size())) {
        return std::nullopt;
    }
    in_.read(value.data(), static_cast<std::streamsize>(value.size()));
    if (in_.gcount() != static_cast<std::streamsize>(value.size())) {
        return std::nullopt;
    }

    if (checksum_for(header, key, value) != header.checksum) {
        return std::nullopt;
    }

    return WalRecord{static_cast<WalOp>(header.op), std::move(key), std::move(value), header.expiry_unix_ms};
}

std::int64_t to_unix_ms(std::chrono::system_clock::time_point time) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}

std::chrono::system_clock::time_point from_unix_ms(std::int64_t unix_ms) {
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(unix_ms));
}
