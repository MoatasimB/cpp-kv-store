# Durable C++ Key-Value Store

A Redis-inspired C++17 key-value store focused on systems fundamentals: TCP networking, TTL expiration, write-ahead logging, snapshot compaction, crash recovery, and benchmarkable behavior.

## What It Implements

- Thread-safe in-memory storage using `std::unordered_map` and `std::shared_mutex`
- TCP server on port `6380`
- Correct line buffering for TCP streams
- Commands: `PING`, `SET`, `GET`, `DEL`, `EXPIRE`, `TTL`, `SAVE`, `COMPACT`, `STATS`, `QUIT`
- Values with spaces, e.g. `SET lesson "hello world"` stores the rest of the line
- TTL expiration with lazy deletion and a background cleanup thread
- Binary write-ahead log with checksums
- Snapshot files for compact durable recovery
- WAL replay on startup
- Storage tests and a concurrent benchmark client

## Architecture

```text
client
  |
  | TCP line protocol
  v
Server
  |
  | parsed commands
  v
KVStore
  |
  | durable mutation records
  v
WAL + Snapshot
```

`KVStore` owns the in-memory map. Mutating operations append to the WAL before updating memory. On restart, the store loads the latest snapshot and replays WAL records to reconstruct state.

Snapshots are compact full copies of live keys. `COMPACT` writes a new snapshot and truncates the WAL so recovery remains fast.

## Build

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

This environment did not have `cmake` installed, so the project was also verified directly with `clang++`:

```bash
clang++ -std=c++17 -Iinclude src/KVStore.cpp src/Wal.cpp src/Server.cpp src/main.cpp -pthread -o kv_store
clang++ -std=c++17 -Iinclude src/KVStore.cpp src/Wal.cpp src/Server.cpp tests/KVStoreTests.cpp -pthread -o kv_tests
clang++ -std=c++17 -Iinclude bench/kv_bench.cpp -pthread -o kv_bench
```

## Run

```bash
./kv_store
```

Then from another terminal:

```bash
nc 127.0.0.1 6380
PING
SET name Moatasim Butt
GET name
EXPIRE name 10
TTL name
STATS
COMPACT
QUIT
```

You can also choose a port and snapshot path:

```bash
./kv_store 6391 /tmp/kv_snapshot.db
```

## Test

```bash
./kv_tests
```

The tests cover WAL recovery, deletion recovery, TTL expiry, and snapshot compaction.

## Benchmark

Start the server first, then run:

```bash
./kv_bench 8 1000 127.0.0.1 6380
```

Arguments are:

```text
kv_bench <clients> <ops_per_client> <host> <port>
```

The benchmark reports throughput and p50/p95/p99 latency for SET+GET request pairs.

## Current Scope

This is currently a durable single-node store with concurrent clients. The next high-value systems extensions are primary-replica replication, consistent hashing across shards, and then Raft-based leader election/log replication for fault tolerance.
