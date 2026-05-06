#include "Wal.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>

WalWriter::WalWriter(const std::string& path, bool truncate)
    : out_(path, truncate ? std::ios::trunc : std::ios::app) {
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
    if (record.op == WalOp::Set) {
        out_ << "SET " << record.expiry_unix_ms << ' '
             << std::quoted(record.key) << ' '
             << std::quoted(record.value) << '\n';
    } else {
        out_ << "DEL " << std::quoted(record.key) << '\n';
    }
    out_.flush();
    if (!out_) {
        throw std::runtime_error("failed to write WAL record");
    }
}

WalReader::WalReader(const std::string& path) : in_(path) {}

std::optional<WalRecord> WalReader::next() {
    if (!in_) {
        return std::nullopt;
    }

    std::string line;
    if (!std::getline(in_, line)) {
        return std::nullopt;
    }

    std::istringstream record(line);
    std::string op;
    record >> op;
    if (op == "SET") {
        WalRecord wal_record;
        wal_record.op = WalOp::Set;
        record >> wal_record.expiry_unix_ms >> std::quoted(wal_record.key) >> std::quoted(wal_record.value);
        if (!record) {
            return std::nullopt;
        }
        return wal_record;
    }
    if (op == "DEL") {
        WalRecord wal_record;
        wal_record.op = WalOp::Del;
        record >> std::quoted(wal_record.key);
        if (!record) {
            return std::nullopt;
        }
        return wal_record;
    }

    if (line.empty()) {
        return std::nullopt;
    }
    throw std::runtime_error("invalid WAL record: " + line);
}

std::int64_t to_unix_ms(std::chrono::system_clock::time_point time) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}

std::chrono::system_clock::time_point from_unix_ms(std::int64_t unix_ms) {
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(unix_ms));
}
