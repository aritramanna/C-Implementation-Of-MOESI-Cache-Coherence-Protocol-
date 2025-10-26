#ifndef MOESI_TYPES_H
#define MOESI_TYPES_H

#include <string>
using namespace std;

enum class State {
    Modified,   // Data is valid, dirty (different from main memory), only in this cache.
    Owned,      // Data is valid, dirty, may be in other caches (not in MESI, but in MOESI)
    Exclusive,  // Data is valid, clean (same as main memory), only in this cache.
    Shared,     // Data is valid, clean, may be in other caches.
    Invalid,    // Data is not valid, must be fetched before use.
};

enum class CpuOp {
    Read,          // Standard load: read data from memory or cache.
    Write,         // Standard store: write data to memory or cache line.
    Atomic_CAS,    // Compare-And-Swap: atomic read-modify-write that updates value if comparison passes.
    Atomic_ADD,    // Atomic Add: atomic increment of the value at the address.
    Atomic_SUB,    // Atomic Subtract: atomic decrement of the value at the address.
    Atomic_AND,    // Atomic And: atomic bitwise and of the value at the address.
    Atomic_OR,     // Atomic Or: atomic bitwise or of the value at the address.
    Atomic_XOR,    // Atomic Xor: atomic bitwise xor of the value at the address.
    Atomic_NAND,   // Atomic Nand: atomic bitwise nand of the value at the address.
    Atomic_NOR,    // Atomic Nor: atomic bitwise nor of the value at the address.
    Atomic_XNOR,   // Atomic Xnor: atomic bitwise xnor of the value at the address.
};

enum class BusOp {
    BusRd,      // Bus Read: Request for a cache line to read (Shared or Exclusive).  Issued on a read miss.
    BusRdX,     // Bus Read Exclusive (Read-for-Ownership): Request for a cache line to perform a write. Fetches the latest data and invalidates all other sharers.
    BusUpgr,    // Bus Upgrade: Request to upgrade an existing Shared line to Modified state without data transfer. Invalidates all other sharers.
    BusWB,      // Bus Write-Back: Write back a Modified or Owned cache line to memory (typically on eviction or replacement).
    None        // No bus operation.
};

struct BusResponse {
    int data;
    bool data_from_memory;
    State requester_new_state;
    bool state_changed;
    State present_state;
    int core_id;  // ID of the core that supplied the data
};

string stateToString(State state) {
    switch (state) {
        case State::Modified: return "M";
        case State::Owned: return "O";
        case State::Exclusive: return "E";
        case State::Shared: return "S";
        case State::Invalid: return "I";
        default: return "Unknown";
    }
}

string cpuOpToString(CpuOp op) {
    switch (op) {
        case CpuOp::Read: return "Read";
        case CpuOp::Write: return "Write";
        case CpuOp::Atomic_CAS: return "Atomic_CAS";
        case CpuOp::Atomic_ADD: return "Atomic_ADD";
        case CpuOp::Atomic_SUB: return "Atomic_SUB";
        case CpuOp::Atomic_AND: return "Atomic_AND";
        case CpuOp::Atomic_OR: return "Atomic_OR";
        case CpuOp::Atomic_XOR: return "Atomic_XOR";
        case CpuOp::Atomic_NAND: return "Atomic_NAND";
        case CpuOp::Atomic_NOR: return "Atomic_NOR";
        case CpuOp::Atomic_XNOR: return "Atomic_XNOR";
        default: return "Unknown";
    }
}

string busOpToString(BusOp op) {
    switch (op) {
        case BusOp::BusRd: return "BusRd";
        case BusOp::BusRdX: return "BusRdX";
        case BusOp::BusUpgr: return "BusUpgr";
        case BusOp::BusWB: return "BusWB";
        case BusOp::None: return "None";
        default: return "Unknown";
    }
}

// Forward declarations
class Processor;
class Bus;

#endif // MOESI_TYPES_H

