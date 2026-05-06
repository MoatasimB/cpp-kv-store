# Durable C++ Key-Value Store

A Redis-inspired C++17 key-value store focused on systems fundamentals: TCP networking, TTL expiration, write-ahead logging, snapshot compaction, crash recovery, and benchmarkable behavior.

## What It Implements

- Thread-safe in-memory storage using `std::unordered_map` and `std::shared_mutex`
- TCP server on port `6380`
- Correct line buffering for TCP streams
- Commands: `PING`, `SET`, `GET`, `DEL`, `EXPIRE`, `TTL`, `SAVE`, `COMPACT`, `STATS`, `QUIT`
- Values with spaces, e.g. `SET lesson "hello world"` stores the rest of the line
- TTL expiration with lazy deletion and a background cleanup thread
- Readable text write-ahead log
- Readable snapshot files for compact durable recovery
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

`KVStore` owns the in-memory map. Mutating operations append readable records to the WAL before updating memory. On restart, the store loads the latest snapshot and replays WAL records to reconstruct state.

WAL and snapshot files use this text format:

```text
SET -1 "name" "Moat"
SET 1770000000000 "temporary" "value"
DEL "old-key"
```

The number after `SET` is the expiration timestamp in Unix milliseconds, or `-1` if the key does not expire.

Snapshots are compact full copies of live keys. `COMPACT` writes a new snapshot and truncates the WAL so recovery remains fast.

## Command Protocol

Each client sends one newline-terminated command at a time:

```text
PING
SET <key> <value>
GET <key>
DEL <key>
EXPIRE <key> <seconds>
TTL <key>
SAVE
COMPACT
STATS
QUIT
```

Responses are also newline-terminated text. Missing keys return `(nil)` for `GET`; `TTL` returns `-2` for missing keys and `-1` for keys without expiration.

`SET` stores the rest of the line as the value, so spaces are supported:

```text
SET full_name Moat
GET full_name
```

## Build

```bash
mkdir build
cd build
cmake ..
cmake --build .
ctest --test-dir .
```

If CMake is not installed, the project can also be built directly with `clang++`:

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
SET name Moat
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

The tests cover:

- WAL recovery after restart
- deletion recovery
- values with spaces
- TTL expiry
- missing/non-expiring TTL behavior
- invalid command handling
- snapshot compaction

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

Example local result on this machine:

```text
clients=4
request_pairs=1000
ops_per_second=57781
p50_pair_us=126
p95_pair_us=191
p99_pair_us=349
```

Benchmark numbers vary by machine, compiler, port contention, and whether persistence files are on a fast local disk.

## Recovery Example

If a client writes:

```text
SET name Moat
SET city NYC
DEL city
```

The WAL records those mutations. On restart, `KVStore` loads the latest snapshot, then replays the WAL in order. The recovered state contains `name = Moat`, and `city` remains deleted.

`COMPACT` writes the current live state into the snapshot file and clears the WAL. Future recovery starts from that compact snapshot plus any newer WAL records.

## Limitations

- Single-node only; no replication, sharding, or consensus yet
- One thread per connected client
- Text WAL is easy to inspect but does not include checksums
- Values with newlines are not supported
- No authentication or TLS
- No eviction policy or memory limit

## Current Scope

This is currently a durable single-node store with concurrent clients. The next high-value systems extensions are primary-replica replication, consistent hashing across shards, and then Raft-based leader election/log replication for fault tolerance.
