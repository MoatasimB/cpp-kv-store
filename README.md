# Durable C++ Key-Value Store

A small Redis-inspired key-value store written in C++17. It supports TCP clients, TTL expiration, readable write-ahead logging, snapshot recovery, tests, and a simple benchmark client.

## Features

- TCP server with newline-based commands
- Thread-safe in-memory storage with `std::unordered_map` and `std::shared_mutex`
- Commands: `PING`, `SET`, `GET`, `DEL`, `EXPIRE`, `TTL`, `SAVE`, `COMPACT`, `STATS`, `QUIT`
- TTL expiration with lazy cleanup and a background cleanup thread
- Readable text WAL for crash recovery
- Snapshot compaction to keep recovery fast
- Unit tests and concurrent benchmark client

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

If CMake is unavailable:

```bash
clang++ -std=c++17 -Iinclude src/KVStore.cpp src/Wal.cpp src/Server.cpp src/main.cpp -pthread -o kv_store
clang++ -std=c++17 -Iinclude src/KVStore.cpp src/Wal.cpp src/Server.cpp tests/KVStoreTests.cpp -pthread -o kv_tests
clang++ -std=c++17 -Iinclude bench/kv_bench.cpp -pthread -o kv_bench
```

## Run

```bash
./build/kv_store
```

Or choose a port and snapshot path:

```bash
./build/kv_store 6391 /tmp/kv_snapshot.db
```

Connect from another terminal:

```bash
nc 127.0.0.1 6380
```

Example commands:

```text
PING
SET name Moat
GET name
EXPIRE name 10
TTL name
STATS
COMPACT
QUIT
```

## Persistence

Writes are appended to a readable WAL before the in-memory map is updated:

```text
SET -1 "name" "Moat"
SET 1770000000000 "temporary" "value"
DEL "old-key"
```

On startup, the store loads the latest snapshot and replays the WAL. `COMPACT` writes the current live keys to a snapshot and clears the WAL.

## Test

```bash
./build/kv_tests
```

The tests cover WAL recovery, deletion recovery, values with spaces, TTL behavior, invalid commands, and snapshot compaction.

## Benchmark

Start the server, then run:

```bash
./build/kv_bench 8 1000 127.0.0.1 6380
```

Arguments:

```text
kv_bench <clients> <ops_per_client> <host> <port>
```

Example result:

```text
clients=4
request_pairs=1000
ops_per_second=57781
p50_pair_us=126
p95_pair_us=191
p99_pair_us=349
```

## Limitations

- Single-node only
- One thread per connected client
- Text WAL has no checksums
- Values with newlines are not supported
- No authentication, TLS, replication, or eviction policy
