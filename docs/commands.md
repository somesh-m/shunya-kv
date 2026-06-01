# Command Reference

This document describes the commands currently implemented in ShunyaKV. The command list is defined in `src/commands.cc`, and the behavior below is based on the corresponding command handlers and tests.

## General Notes

- Commands are case-insensitive at dispatch time. For example, `set`, `SET`, and `SeT` all resolve to the same handler.
- The server speaks RESP and returns standard RESP simple strings, bulk strings, null bulk strings, or RESP errors depending on the command result.
- If a command name is not implemented, the server returns `-ERR unknown command`.

## `SET`

Store a value for a key.

### Syntax

```text
SET <key> <value>
SET <key> <value> EX <ttl>
```

### Parameters

- `key`: required, must be non-empty.
- `value`: required, may be empty.
- `EX`: optional, case-insensitive TTL modifier.
- `ttl`: optional, must be a positive integer.

### Behavior

- Stores or overwrites the value for `key`.
- If `EX <ttl>` is provided, the key expires after `ttl`.
- Expiration is handled lazily on access.
- Overwriting a key with another `SET ... EX ...` refreshes the TTL.
- Overwriting a key with plain `SET <key> <value>` preserves the existing TTL rather than clearing it.

### Responses

- Success: `+OK`
- Invalid arity: `-ERR wrong number of arguments for 'SET'`
- Empty key: `-ERR empty key`
- Invalid TTL modifier: `-ERR syntax error (expected EX)`
- Invalid TTL value: `-ERR invalid expire time`
- Other syntax mismatch: `-ERR syntax error`
- Internal store failure: `-NOT STORED`

### Examples

```text
SET user:1 alice
SET session:42 payload EX 300
```

## `GET`

Fetch the value for a key.

### Syntax

```text
GET <key>
```

### Parameters

- `key`: required, must be non-empty.

### Behavior

- Returns the stored value if the key exists.
- Returns null if the key does not exist.
- If the key has expired, it is removed and treated as missing.

### Responses

- Hit: RESP bulk string, for example `$4\r\nTEST\r\n`
- Miss: RESP null bulk string, `$-1`
- Invalid arity: `-ERR wrong number of arguments for 'GET'`
- Empty key: `-ERR empty key`

### Example

```text
GET user:1
```

## `QUIT`

Close the client connection.

### Syntax

```text
QUIT
```

### Behavior

- Returns `BYE` and the server closes the connection after the command succeeds.
- The current handler does not validate argument count, so extra arguments are ignored by the implementation.

### Response

- Success: `+BYE`

## `NODE_INFO`

Return shard and hash-range metadata for the current node.

### Syntax

```text
NODE_INFO
NODE_INFO <shard_id>
NODE_INFO MAP
NODE_INFO RANGES
NODE_INFO SHARDS
```

### Request Forms

- `NODE_INFO` returns the details of the shard handling the current connection.
- `NODE_INFO <shard_id>` returns the details of the shard identified by the passed shard id.
- `NODE_INFO MAP` and `NODE_INFO RANGES` return the information for all shards.
- `NODE_INFO SHARDS` returns the total number of shards.

### Parameters

- No argument: returns info for the shard handling the current connection.
- `<shard_id>`: returns info for a specific shard.
- `MAP` or `RANGES`: returns the full shard-to-hash-range map.
- `SHARDS`: returns the configured shard count as a bulk string.

### Behavior

- `NODE_INFO` returns a JSON object with:
  - `epoch`
  - `smp`
  - `base_port`
  - `port_offset`
  - `hash`
  - `current_shard`
  - `ranges`
- `NODE_INFO MAP` and `NODE_INFO RANGES` return a JSON object with:
  - `epoch`
  - `smp`
  - `base_port`
  - `port_offset`
  - `hash`
  - `ranges`
- Each `ranges` entry is encoded as:

```text
["<lo>","<hi>",<shard>,<port>]
```

- `lo` and `hi` are inclusive 64-bit hash boundaries serialized as strings.

### Responses

- Success: RESP bulk string containing JSON or the shard count.
- Invalid arity: `-ERR wrong number of arguments for 'NODE_INFO'`

### Example Payloads

Single-shard view:

```json
{
  "epoch": 1,
  "smp": 4,
  "base_port": 60110,
  "port_offset": 0,
  "hash": "fnv1a435345",
  "current_shard": 2,
  "ranges": [["9223372036854775808","13835058055282163711",2,60112]]
}
```

Full map view:

```json
{
  "epoch": 1,
  "smp": 4,
  "base_port": 60110,
  "port_offset": 0,
  "hash": "fnv1a435345",
  "ranges": [
    ["0","4611686018427387903",0,60110],
    ["4611686018427387904","9223372036854775807",1,60111]
  ]
}
```

## `INFO`

Return runtime and memory statistics.

### Syntax

```text
INFO
INFO JSON
```

### Parameters

- No argument: returns a text report.
- `JSON`: returns a JSON-formatted report.

### Behavior

- Gathers statistics from all shards and returns per-shard data plus cumulative totals.
- The plain-text format includes per-shard sections followed by a total section.
- The JSON format includes:
  - `shard_info`: array of per-shard objects
  - `shard_cumulative_info`: total values across shards

### Plain-Text Fields

Per shard:

- `shard_no`
- `allocated_memory`
- `free_memory`
- `total_memory`
- `total_allocs`
- `failed_allocs`
- `pool_total_slots`
- `pool_available_slots`
- `pool_fallback_allocs`
- `key_count`
- `cache_miss`
- `eviction_count`

Totals:

- `allocated_memory`
- `free_memory`
- `total_memory`
- `total_allocs`
- `failed_allocs`

### JSON Fields

Per shard objects include:

- `shard_id`
- `allocated_memory`
- `free_memory`
- `total_memory`
- `total_allocs`
- `failed_allocs`
- `pool_total_slots`
- `pool_available_slots`
- `pool_fallback_allocs`
- `key_count`
- `cache_miss`
- `eviction_count`
- `prob_pool_total_slots`
- `prob_pool_used_slots`
- `prob_pool_eviction_count`

Cumulative totals include the same aggregate counters except `shard_id`.

### Responses

- Success: RESP bulk string containing either text or JSON
- Too many arguments: `-ERR wrong number of arguements for 'INFO'`
- Invalid sub-argument: `-Invalid sub arg for 'INFO'`

### Examples

```text
INFO
INFO JSON
```
