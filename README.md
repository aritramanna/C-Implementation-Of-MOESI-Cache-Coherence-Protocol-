# MOESI Cache Coherence Protocol Simulator

A comprehensive C++ implementation of the MOESI (Modified, Owned, Exclusive, Shared, Invalid) cache coherence protocol with multi-threaded atomic operation support.

## Overview

This project simulates a multi-processor system with private L1 caches that maintain coherence using the MOESI protocol. The simulator models bus-based snooping operations and demonstrates how cache states transition based on read, write, and atomic operations across multiple CPU cores.

## Features

- **5-State Cache Coherence**: Full implementation of Modified, Owned, Exclusive, Shared, and Invalid states
- **Multi-Processor Support**: Simulates 4 CPU cores with independent L1 caches
- **Bus Snooping**: Complete bus-based coherence protocol with snooping logic
- **Atomic Operations**: Thread-safe atomic operations including:
  - Atomic ADD, SUB, AND, OR, XOR
  - Atomic NAND, NOR, XNOR
  - Compare-And-Swap (CAS)
- **Direct-Mapped Cache**: 64-line cache per processor
- **Write-Back Policy**: Dirty cache lines written back on eviction
- **Comprehensive Testing**: 21+ test scenarios covering all state transitions

## MOESI Protocol States

| State | Description |
|-------|-------------|
| **Modified (M)** | Cache line is dirty and exclusive to this cache |
| **Owned (O)** | Cache line is dirty but shared with other caches |
| **Exclusive (E)** | Cache line is clean and exclusive to this cache |
| **Shared (S)** | Cache line is clean and may exist in other caches |
| **Invalid (I)** | Cache line is not valid |

## State Transition Diagram

```
Read Miss (no sharers)  → E
Read Miss (with sharers) → S
Read Hit (E/S/O/M)      → No change
Write Miss              → M
Write Hit (S/O)         → M (BusUpgr)
Write Hit (E)           → M (no bus op)
Write Hit (M)           → M (no bus op)
Snoop Read (M)          → O
Snoop Read (E)          → S
Snoop Write             → I
```

## Bus Operations

- **BusRd**: Read request from another processor
- **BusRdX**: Read-for-ownership (exclusive read)
- **BusUpgr**: Upgrade from Shared/Owned to Modified
- **BusWB**: Write-back dirty data to memory

## Architecture

```
┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐
│   CPU-0     │  │   CPU-1     │  │   CPU-2     │  │   CPU-3     │
│  (64-line   │  │  (64-line   │  │  (64-line   │  │  (64-line   │
│   L1 Cache) │  │   L1 Cache) │  │   L1 Cache) │  │   L1 Cache) │
└──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘
       │                │                │                │
       └────────────────┴────────────────┴────────────────┘
                            │
                    ┌───────▼───────┐
                    │  Shared Bus   │
                    │  (Snooping)   │
                    └───────┬───────┘
                            │
                    ┌───────▼───────┐
                    │ Shared Memory │
                    │  (2048 words) │
                    └───────────────┘
```

## Building and Running

### Prerequisites

- C++11 or later
- GCC/Clang compiler with threading support
- POSIX threads library

### Compile

```bash
g++ -std=c++11 -pthread moesi.cpp -o moesi
```

### Run

```bash
./moesi
```

## Test Scenarios

The simulator includes comprehensive test suites:

### Basic Read/Write Tests (21 scenarios)

1. Multiple CPUs reading same address (Shared state creation)
2. Reading from already Shared cache
3. Writing to Shared cache (BusUpgr)
4. Write operation invalidating Shared copies
5. Reading from Modified cache (M→O transition)
6. Exclusive state creation
7. Writing to Exclusive cache (E→M transition)
8. Reading from Modified cache to create Owned state
9. Writing to Owned cache (O→M transition)
10. Conflict miss with dirty data (BusWB)
11. Exclusive → Invalid transition
12. Owned → Invalid transition
13. Read-Modify-Write sequences
14. Complex multi-core scenarios

### Atomic Operations Test

- 4 threads incrementing a shared counter
- Demonstrates thread-safe atomic operations
- Validates cache coherence under concurrent access
- Shows final cache line state across all cores

## Example Output

```
========================================
CPU - 0: Executing Instruction: Read @ addr 0x4
========================================
CPU - 0: Cache-MISS @ addr 0x4 (index 1) | initial state: Invalid
CPU - 0: Sending Bus Request | BusRd @ addr 0x4
CPU - 0: Requester Bus Response Received | data: 0x1111 | from: memory
CPU - 0: Requesting Cache-Line Transition | [Invalid->Exclusive]

========================================
CPU - 1: Executing Instruction: Write @ addr 0x4 | data: 0x9999
========================================
CPU - 1: Cache-MISS @ addr 0x4 (index 1) | initial state: Invalid
CPU - 1: Sending Bus Request | BusRdX @ addr 0x4
CPU - 0: Snooped Cache-HIT @ addr 0x4 (index 1) | state: Exclusive
CPU - 0: Snooped Cache-Line Transition | [Exclusive->Invalid]
CPU - 1: Requester Bus Response Received | data: 0x1111
CPU - 1: Requesting Cache-Line Transition | [Invalid->Modified]
CPU - 1: Write completed | value: 0x9999 | final state: Modified
```

## Key Implementation Details

### Thread Safety

- Global mutex (`operation_mutex`) serializes all CPU operations
- Prevents race conditions during bus operations
- Atomic operations execute with bus lock semantics

### Cache Indexing

- Direct-mapped cache: `index = (address / 4) % CACHE_SIZE`
- Ignores byte offset within word (lower 2 bits)
- Conflict misses trigger write-back if dirty

### Snoop Priority

When multiple caches respond to a bus operation, priority order:
1. Modified (highest) - always provides data
2. Owned - provides data if no Modified
3. Exclusive - transitions to Shared
4. Shared - no state change
5. Invalid (lowest) - no response

## Memory Configuration

- **Memory Size**: 2048 Bytes (2KB)
- **Cache Size**: 64 lines per processor
- **Number of Processors**: 4
- **Mapping**: Direct-mapped cache
- **Write Policy**: Write-back with write-allocate

## Files

- `moesi.cpp` - Main implementation with processor, cache, and bus logic
- `moesi_types.h` - Enum definitions for states, operations, and helper functions

## Verification Points

✅ All MOESI state transitions  
✅ Bus snooping operations  
✅ Cache-to-cache transfers  
✅ Write-back on dirty eviction  
✅ Atomic operation atomicity  
✅ Multi-threaded cache coherence  
✅ Priority-based snoop responses  

## References

- [Cache Coherence Protocols](https://en.wikipedia.org/wiki/MOESI_protocol)
- [Memory Consistency and Cache Coherence](https://www.morganclaypool.com/doi/abs/10.2200/S00346ED1V01Y201104CAC016)
- [A Primer on Memory Consistency and Cache Coherence](https://link.springer.com/book/10.1007/978-3-031-01764-3)

## License

This project is provided for educational purposes.

## Author

Created as a verification and architecture learning project to understand cache coherence protocols in multi-processor systems.

---

**Note**: This simulator uses a global mutex to serialize operations for correctness demonstration. Real hardware implements fine-grained cache coherence without global locks.

