# QuantArena Integration Guide

## Overview

ExchangeCore supports the **QuantArena Daily Match Platform** protocol.
When a match seed is supplied, the engine enters deterministic mode — every random number, simulated timestamp, and order-book event is fully determined by that seed. Running twice with the same seed + same submitted orders produces byte-identical `trades.jsonl` logs.

---

## Seeded Match Mode

### Seed Format

| Bits | Hex chars | Example |
|------|-----------|---------|
| 64   | 16        | `deadbeefcafe1234` |
| 128  | 32        | `deadbeefcafe12340011223344556677` |
| 256  | 64        | `deadbeef...` (64 chars) |

- Optional `0x`/`0X` prefix is stripped automatically.
- Whitespace is stripped automatically (paste-friendly).
- Regex validated: `^[0-9a-fA-F]{16,64}$` (must be 16, 32, or 64 chars after stripping).

### PRNG

xoshiro256\*\* seeded by splatting the hex seed directly into the 4x64-bit state. Portable and bit-identical across MSVC/GCC/Clang.

### Simulated Clock

- Time starts at `1_000_000_000 ns` (1 second past epoch).
- Advances by a PRNG-drawn `[1, 1000]` nanoseconds on every call to `SessionManager::now()`.
- `ts_ns` in JSONL is monotonically non-decreasing and fits in `uint64_t`.
- Wall-clock is never used in the matching/simulation path.

---

## CLI Usage (Headless Mode)

```
ExchangeCore.exe --headless --seed <hex> --match-id <id> --duration-sec <N>
```

| Flag | Default | Description |
|------|---------|-------------|
| `--headless` | off | Run without TUI, exit after duration |
| `--seed <hex>` | (none) | Match seed, enables deterministic mode |
| `--match-id <id>` | `default_match` | Used as the export directory name |
| `--duration-sec <N>` | `300` | Wall-clock seconds to run simulation |
| `--help` | | Print usage |

Output: `./quantarena_exports/<match-id>/`

---

## TUI Usage (Interactive Mode)

1. Launch `ExchangeCore.exe` (no flags)
2. Navigate to **Match Session** in the left sidebar
3. Type or paste the match seed into the **Match Seed (Hex)** field
4. Press **[START QUANTARENA MATCH]** to restart engine with the seed
5. Trade freely; bot activity is deterministic
6. Press **[END MATCH]** to stop simulator
7. Press **[EXPORT QUANTARENA LOG]** to write the export files

---

## Export Format

### `trades.jsonl`

One JSON object per line (LF-terminated):

```jsonl
{"seq":0,"ts_ns":"1000000001","symbol":"AAPL","side":"buy","qty":10,"price":150.0,"order_type":"limit","order_id":"o42"}
```

| Field | Type | Notes |
|-------|------|-------|
| `seq` | integer | 0-indexed, monotone |
| `ts_ns` | string | uint64, nanoseconds since epoch |
| `symbol` | string | From symbol universe |
| `side` | string | `"buy"` or `"sell"` (taker side) |
| `qty` | number | Serialized with `%.10g` |
| `price` | number | Serialized with `%.10g` |
| `order_type` | string | `"limit"` or `"market"` |
| `order_id` | string | `o<N>` format, max 64 chars |

Limits: max 100,000 lines, max 8 MB.

### `trades.jsonl.sha256`

SHA-256 hex digest of `trades.jsonl` raw bytes, followed by `\n`.

### `manifest.json`

```json
{
  "match_id": "...",
  "seed_hex": "...",
  "engine_version": "...",
  "line_count": 12345,
  "byte_count": 654321,
  "sha256": "...",
  "opens_at": "...",
  "closes_at": "...",
  "starting_capital": 100000
}
```

---

## Symbol Universe

Default: `AAPL`, `MSFT`, `GOOG`, `BTCUSD`, `ETHUSD`.

Orders with unknown symbols are rejected with `OrderRejectedEvent` (reason: `"Symbol not in active universe"`).

---

## Determinism Contract

Running with the same seed + same user-submitted order sequence **must produce byte-identical** `trades.jsonl` (verifiable via SHA-256). Bot activity is also fully reproducible since it uses the same xoshiro256** PRNG seeded from the match seed.
