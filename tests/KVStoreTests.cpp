#include "KVStore.hpp"
#include "Server.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>

namespace {
struct TestPaths {
    std::string snapshot;
    std::string wal;
};

TestPaths make_paths(const std::string& name) {
    const auto base_path = std::filesystem::temp_directory_path() /
                           ("cpp_kv_store_" + name + "_" + std::to_string(getpid()));
    const auto base = base_path.string();
    std::remove(base.c_str());
    std::remove((base + ".wal").c_str());
    std::remove((base + ".tmp").c_str());
    return TestPaths{base, base + ".wal"};
}

void cleanup(const TestPaths& paths) {
    std::remove(paths.snapshot.c_str());
    std::remove(paths.wal.c_str());
    std::remove((paths.snapshot + ".tmp").c_str());
}

void test_wal_recovers_set_and_delete() {
    const auto paths = make_paths("wal");
    {
        KVStore store(paths.snapshot, paths.wal);
        store.set("alpha", "one");
        store.set("beta", "two words");
        assert(store.del("alpha"));
    }

    std::remove(paths.snapshot.c_str());
    {
        KVStore recovered(paths.snapshot, paths.wal);
        assert(!recovered.get("alpha").has_value());
        assert(recovered.get("beta").value() == "two words");
    }
    cleanup(paths);
}

void test_wal_recovers_after_restart_without_snapshot() {
    const auto paths = make_paths("restart");
    {
        KVStore store(paths.snapshot, paths.wal);
        store.set("session", "still here");
    }

    std::remove(paths.snapshot.c_str());
    {
        KVStore recovered(paths.snapshot, paths.wal);
        assert(recovered.get("session").value() == "still here");
    }
    cleanup(paths);
}

void test_values_with_spaces_through_command_handler() {
    const auto paths = make_paths("spaces");
    {
        KVStore store(paths.snapshot, paths.wal);
        assert(Server::handle_command(store, "SET full_name Moat") == "OK\n");
        assert(Server::handle_command(store, "GET full_name") == "Moat\n");
    }
    cleanup(paths);
}

void test_ttl_expires() {
    const auto paths = make_paths("ttl");
    {
        KVStore store(paths.snapshot, paths.wal);
        store.set("short", "lived");
        assert(store.expire("short", 1));
        assert(store.ttl("short").has_value());
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        assert(!store.get("short").has_value());
    }
    cleanup(paths);
}

void test_ttl_missing_and_non_expiring_keys() {
    const auto paths = make_paths("ttl_values");
    {
        KVStore store(paths.snapshot, paths.wal);
        assert(!store.ttl("missing").has_value());
        store.set("permanent", "value");
        assert(store.ttl("permanent").value() == -1);
    }
    cleanup(paths);
}

void test_invalid_commands_return_errors() {
    const auto paths = make_paths("invalid_commands");
    {
        KVStore store(paths.snapshot, paths.wal);
        assert(Server::handle_command(store, "") == "ERR empty command\n");
        assert(Server::handle_command(store, "BOGUS") == "ERR unknown command\n");
        assert(Server::handle_command(store, "GET too many args") == "ERR usage: GET key\n");
        assert(Server::handle_command(store, "EXPIRE key nope") == "ERR seconds must be an integer\n");
    }
    cleanup(paths);
}

void test_lru_evicts_least_recently_used_key() {
    const auto paths = make_paths("lru");
    {
        KVStore store(paths.snapshot, paths.wal, 2);
        store.set("a", "1");
        store.set("b", "2");
        assert(store.get("a").value() == "1");
        store.set("c", "3");

        assert(store.get("a").value() == "1");
        assert(!store.get("b").has_value());
        assert(store.get("c").value() == "3");
    }
    cleanup(paths);
}

void test_lru_eviction_is_recovered_from_wal() {
    const auto paths = make_paths("lru_recovery");
    {
        KVStore store(paths.snapshot, paths.wal, 2);
        store.set("a", "1");
        store.set("b", "2");
        store.set("c", "3");
    }

    std::remove(paths.snapshot.c_str());
    {
        KVStore recovered(paths.snapshot, paths.wal, 2);
        assert(!recovered.get("a").has_value());
        assert(recovered.get("b").value() == "2");
        assert(recovered.get("c").value() == "3");
    }
    cleanup(paths);
}

void test_snapshot_compaction_recovers_live_data() {
    const auto paths = make_paths("snapshot");
    {
        KVStore store(paths.snapshot, paths.wal);
        store.set("keep", "value");
        store.set("gone", "value");
        assert(store.del("gone"));
        store.compact();
    }
    {
        KVStore recovered(paths.snapshot, paths.wal);
        assert(recovered.get("keep").value() == "value");
        assert(!recovered.get("gone").has_value());
    }
    cleanup(paths);
}
}  // namespace

int main() {
    test_wal_recovers_set_and_delete();
    test_wal_recovers_after_restart_without_snapshot();
    test_values_with_spaces_through_command_handler();
    test_ttl_expires();
    test_ttl_missing_and_non_expiring_keys();
    test_invalid_commands_return_errors();
    test_lru_evicts_least_recently_used_key();
    test_lru_eviction_is_recovered_from_wal();
    test_snapshot_compaction_recovers_live_data();
    std::cout << "KVStore tests passed\n";
    return 0;
}
