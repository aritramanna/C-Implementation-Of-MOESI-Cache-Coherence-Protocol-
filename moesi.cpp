#include <iostream>
#include <array>
#include <random>
#include <string>
#include <thread>
#include <mutex>
#include "moesi_types.h"

using namespace std;

#define MEMORY_SIZE 2048
#define CACHE_SIZE 64
#define NUM_PROCESSORS 4

// Global memory shared by all processors
array<int, MEMORY_SIZE> memory = {0};

// Global mutex to serialize all CPU operations
mutex operation_mutex;

class CacheLine {
public:
    int address;
    int value;
    State state;

    CacheLine () {
        address = -1;
        value = 0;
        state = State::Invalid;
    }
};

// Logical Processor Cache.

class Processor {

private:
    int id;
    Bus* bus;  // Reference to the shared bus
    
    // Helper function to calculate cache index (direct-mapped)
    // Ignores lower 2 bits (byte offset within DW) and uses modulo CACHE_SIZE
    int getCacheIndex(int address) const {
        return (address / 4) % CACHE_SIZE;  // addr[31:2] % CACHE_SIZE
    }
    
public:
    array<CacheLine, CACHE_SIZE> cache;  // Local L1 Cache for Logical Processor.

    Processor(int id = 0, Bus* b = nullptr) : id(id), bus(b) {
    }

    void printCacheLine(const int& address) {
        int index = getCacheIndex(address);
        cout << "CPU - " << id << ": Cache line " << index << ": address=" << cache[index].address << " value=" << cache[index].value << " state=" << stateToString(cache[index].state) << endl;
    }

    // Handle cache eviction with write-back for dirty data
    void handleCacheEviction(const int& new_address, const int& cache_index) {
        bool conflict_miss = (cache[cache_index].state != State::Invalid) && (cache[cache_index].address != new_address);
        
        if (conflict_miss && (cache[cache_index].state == State::Modified || cache[cache_index].state == State::Owned)) {
            // Write back dirty data to memory before evicting
            int old_address = cache[cache_index].address;
            int old_value = cache[cache_index].value;
            
            cout << "CPU - " << id << ": Conflict miss detected with dirty data | write-back required" << endl;
            cout << "CPU - " << id << ": Sending Bus Request | BusWB @ addr 0x" << hex << old_address << dec << endl;
            send_bus_operation(BusOp::BusWB, old_address, id);
            cout << "CPU - " << id << ": Write-back completed | data: 0x" << hex << old_value << dec << " written to memory" << endl;
            
            // Invalidate the evicted line
            cache[cache_index].state = State::Invalid;
        }
    }

    // Perform atomic operation on cache value
    void performAtomicOperation(const CpuOp& op, const int& value, const int& cache_index, const int& expected_value = 0) {
        int old_value = cache[cache_index].value;
        switch(op) {
            case CpuOp::Atomic_CAS: 
                // Compare-And-Swap: if current value matches expected, replace with new value
                if (cache[cache_index].value == expected_value) {
                    cache[cache_index].value = value;
                } // else do nothing - CAS failed
                break;
            case CpuOp::Atomic_ADD: 
                cache[cache_index].value += value; 
                break;
            case CpuOp::Atomic_SUB: 
                cache[cache_index].value -= value; 
                break;
            case CpuOp::Atomic_AND: 
                cache[cache_index].value &= value; 
                break;
            case CpuOp::Atomic_OR:  
                cache[cache_index].value |= value; 
                break;
            case CpuOp::Atomic_XOR: 
                cache[cache_index].value ^= value; 
                break;
            case CpuOp::Atomic_NAND: 
                cache[cache_index].value = ~(cache[cache_index].value & value); 
                break;
            case CpuOp::Atomic_NOR:  
                cache[cache_index].value = ~(cache[cache_index].value | value); 
                break;
            case CpuOp::Atomic_XNOR: 
                cache[cache_index].value = ~(cache[cache_index].value ^ value); 
                break;
            default: 
                break;
        }
        cout << "CPU - " << id << ": Performing atomic operation | type: " << cpuOpToString(op) 
             << " | old value: 0x" << hex << old_value << dec 
             << " | operand: 0x" << hex << value << dec 
             << " | new value: 0x" << hex << cache[cache_index].value << dec << endl;
    }

    void cpu_operation(const CpuOp& op, const int& address, const int& value = 0, const int& expected_value = 0) {
        // Lock the entire CPU operation to prevent thread interleaving
        lock_guard<mutex> lock(operation_mutex);

        cout << "========================================" << endl;
        if (op == CpuOp::Write) {
            cout << "CPU - " << id << ": Executing Instruction: " << cpuOpToString(op) << " @ addr 0x" << hex << address << dec << " | data: 0x" << hex << value << dec << endl;
        } else {
            cout << "CPU - " << id << ": Executing Instruction: " << cpuOpToString(op) << " @ addr 0x" << hex << address << dec << endl;
        }
        cout << "========================================" << endl;
        
        int index = getCacheIndex(address);
        
        switch (op) {
            case CpuOp::Read: {

                // Check for cache hit: valid state AND matching address
                bool is_hit = (cache[index].state != State::Invalid) && (cache[index].address == address);
                
                // Print 1: CPU Request (Hit/Miss) with initial state
                if (!is_hit) {
                    cout << "CPU - " << id << ": Cache-MISS @ addr 0x" << hex << address << dec << " (index " << index << ") | initial state: " << stateToString(cache[index].state) << endl;
                } else {
                    cout << "CPU - " << id << ": Cache-HIT @ addr 0x" << hex << address << dec << " (index " << index << ") | initial state: " << stateToString(cache[index].state) << endl;
                }
                
                if (!is_hit) {
                    // Handle cache eviction with write-back for dirty data
                    handleCacheEviction(address, index);
                    
                    // Save present state before bus operation
                    State present_state = cache[index].state;
                    
                    // Print Bus Request
                    cout << "CPU - " << id << ": Sending Bus Request | BusRd @ addr 0x" << hex << address << dec << endl;

                    // Issue BusRd transaction to the bus.
                    BusResponse response = send_bus_operation(BusOp::BusRd, address, id);
                    
                    // Update cache with fetched data
                    cache[index].address = address;  // Store full address
                    cache[index].value = response.data;
                    cache[index].state = response.requester_new_state;

                    // Print 2: Bus Response received
                    string dataSource = response.data_from_memory ? "memory" : "CPU-" + to_string(response.core_id);
                    cout << "CPU - " << id << ": Requester Bus Response Received | data: 0x" << hex << response.data << dec 
                         << " | from: " << dataSource << endl;
                    
                    // Print 3: Requesting Cache-Line Transition
                    cout << "CPU - " << id << ": Requesting Cache-Line Transition | [" << stateToString(present_state) 
                         << "->" << stateToString(cache[index].state) << "]" << endl;
                    
                } else {
                    // Read Hit - no bus operation needed
                    // Print 2: No bus operation needed
                    cout << "CPU - " << id << ": Local Cache Hit Received | data: 0x" << hex << cache[index].value << dec
                         << " | from: local cache | state: " << stateToString(cache[index].state) << endl;
                    
                    // Print 3: Requesting Cache-Line Transition (no change)
                    State present_state = cache[index].state;
                    cout << "CPU - " << id << ": Requesting Cache-Line Transition | [" << stateToString(present_state) 
                         << "->" << stateToString(present_state) << "]" << endl;
                }
                break;
            }
            case CpuOp::Write: {
                // Write operation: Check for cache hit, send BusRdX if miss, BusUpgr if Shared
                
                // Check for cache hit: valid state AND matching address
                bool is_hit = (cache[index].state != State::Invalid) && (cache[index].address == address);
                
                // Print 1: CPU Request (Hit/Miss) with initial state
                if (!is_hit) {
                    cout << "CPU - " << id << ": Cache-MISS @ addr 0x" << hex << address << dec << " (index " << index << ") | initial state: " << stateToString(cache[index].state) << endl;
                } else {
                    cout << "CPU - " << id << ": Cache-HIT @ addr 0x" << hex << address << dec << " (index " << index << ") | initial state: " << stateToString(cache[index].state) << endl;
                }
                
                if (!is_hit) {
                    // Handle cache eviction with write-back for dirty data
                    handleCacheEviction(address, index);
                    
                    // Cache miss: Send BusRdX to get exclusive ownership
                    State present_state = cache[index].state;
                    
                    // Print Bus Request
                    cout << "CPU - " << id << ": Sending Bus Request | BusRdX @ addr 0x" << hex << address << dec << endl;
                    
                    // Send bus operation and get response
                    BusResponse response = send_bus_operation(BusOp::BusRdX, address, id);
                    
                    // Update cache address and state 
                    cache[index].address = address;  // Store full address
                    cache[index].state = response.requester_new_state;
                    
                    // Fetch data from bus response first
                    cache[index].value = response.data;

                    cout << "CPU - " << id << ": Requester Bus Response Received | data: 0x" << hex << response.data << endl;
                    
                    // Print 3: Requesting Cache-Line Transition (no bus response print needed for writes)
                    cout << "CPU - " << id << ": Requesting Cache-Line Transition | [" << stateToString(present_state) 
                         << "->" << stateToString(cache[index].state) << "]" << endl;
                    
                    // Now write data to the cache line (overwrite fetched data)
                    cache[index].value = value;
                    
                } else if (cache[index].state == State::Shared) {
                    // Cache hit in Shared state: Send BusUpgr to invalidate other copies
                    // Shared state cannot be written directly - must invalidate all other copies first
                    State present_state = cache[index].state;
                    
                    // Print Bus Request
                    cout << "CPU - " << id << ": Sending Bus Request | BusUpgr @ addr 0x" << hex << address << dec << endl;
                    
                    // Send bus operation and get response
                    BusResponse response = send_bus_operation(BusOp::BusUpgr, address, id);
                    
                    // Print 2: Bus Response received (no data needed for BusUpgr)
                    cout << "CPU - " << id << ": Requester Bus Response Received | BusUpgr completed" << endl;
                    
                    // Print 3: Requesting Cache-Line Transition
                    cout << "CPU - " << id << ": Requesting Cache-Line Transition | [" << stateToString(present_state) 
                         << "->" << stateToString(State::Modified) << "]" << endl;
                    
                    // Write to own cacheline and transition to Modified
                    cache[index].value = value;
                    cache[index].state = State::Modified;
                    
                } else if (cache[index].state == State::Exclusive) {
                    // Cache hit in Exclusive state: No bus operation needed (already has exclusive ownership)
                    State present_state = cache[index].state;
                    
                    cout << "CPU - " << id << ": No bus operation needed | already has exclusive ownership" << endl;
                    
                    // Write to own cacheline and transition to Modified
                    cout << "CPU - " << id << ": Requesting Cache-Line Transition | [" << stateToString(present_state) 
                         << "->" << stateToString(State::Modified) << "]" << endl;
                    
                    cache[index].value = value;
                    cache[index].state = State::Modified;
                } else if (cache[index].state == State::Owned) {
                    // Cache hit in Owned state: Send BusUpgr to invalidate other copies, transition O->M
                    State present_state = cache[index].state;
                    
                    cout << "CPU - " << id << ": Sending Bus Request | BusUpgr @ addr 0x" << hex << address << dec << endl;
                    
                    // Send bus operation and get response
                    BusResponse response = send_bus_operation(BusOp::BusUpgr, address, id);
                    
                    // Print 2: Bus Response received (no data needed for BusUpgr)
                    cout << "CPU - " << id << ": Requester Bus Response Received | BusUpgr completed" << endl;
                    
                    // Print 3: Requesting Cache-Line Transition
                    cout << "CPU - " << id << ": Requesting Cache-Line Transition | [" << stateToString(present_state) 
                         << "->" << stateToString(State::Modified) << "]" << endl;
                    
                    // Write to own cacheline and transition to Modified
                    cache[index].value = value;
                    cache[index].state = State::Modified;
                    
                } else if (cache[index].state == State::Modified) {
                    // Cache hit in Modified state: No bus operation needed (already has exclusive ownership)
                    cout << "CPU - " << id << ": No bus operation needed | already Modified" << endl;                    
                    cache[index].value = value;
                }
                
                cout << "CPU - " << id << ": Write completed | value: 0x" << hex << value << dec << " | final state: " << stateToString(cache[index].state) << endl;
                break;
            }
            // Atomic operations
            case CpuOp::Atomic_CAS:
            case CpuOp::Atomic_ADD:
            case CpuOp::Atomic_SUB:
            case CpuOp::Atomic_AND:
            case CpuOp::Atomic_OR:
            case CpuOp::Atomic_XOR:
            case CpuOp::Atomic_NAND:
            case CpuOp::Atomic_NOR:
            case CpuOp::Atomic_XNOR:
            {
                int index = getCacheIndex(address);
                bool is_hit = (cache[index].address == address) && (cache[index].state != State::Invalid);
                
                cout << "\n>>> CPU - " << id << ": ACQUIRED BUS LOCK | Executing Atomic Operation " << cpuOpToString(op) << " @ addr 0x" << hex << address << dec << endl;
                
                if (!is_hit) {
                    // Cache miss - check for eviction and handle conflict miss
                    handleCacheEviction(address, index);
                    
                    // Cache miss: Send BusRdX to get exclusive ownership
                    State present_state = cache[index].state;
                    
                    cout << "CPU - " << id << ": Sending Bus Request | BusRdX @ addr 0x" << hex << address << dec << endl;
                    
                    // Send bus operation and get response
                    BusResponse response = send_bus_operation(BusOp::BusRdX, address, id);
                    
                    // Update cache address and state (not the value - we'll write that below)
                    cache[index].address = address;  // Store full address
                    cache[index].state = State::Modified;
                    
                    cout << "CPU - " << id << ": Requesting Cache-Line Transition | [" << stateToString(present_state) 
                         << "->" << stateToString(State::Modified) << "]" << endl;
                    
                    // Fetch data from response
                    cache[index].value = response.data;

                    // Perform atomic operation (write occurs here)
                    performAtomicOperation(op, value, index, expected_value);
                    
                    // State is already Modified (from line 314)
                    
                } else if (cache[index].state == State::Shared || cache[index].state == State::Owned) {
                    // Cache hit in Shared or Owned state: Send BusUpgr to invalidate other copies
                    State present_state = cache[index].state;
                    
                    cout << "CPU - " << id << ": Sending Bus Request | BusUpgr @ addr 0x" << hex << address << dec << endl;
                    
                    // Send bus operation and get response
                    BusResponse response = send_bus_operation(BusOp::BusUpgr, address, id);
                    
                    // Perform atomic operation first (write occurs here)
                    performAtomicOperation(op, value, index, expected_value);
                    
                    // Then transition to Modified state after atomic write completes
                    cache[index].state = State::Modified;
                    
                    cout << "CPU - " << id << ": Requesting Cache-Line Transition | [" << stateToString(present_state) 
                         << "->" << stateToString(State::Modified) << "]" << endl;
                    
                } else if (cache[index].state == State::Modified || cache[index].state == State::Exclusive) {
                    // Already have exclusive ownership
                    cout << "CPU - " << id << ": No bus operation needed | already has exclusive ownership" << endl;
                    
                    // Perform atomic operation first (write occurs here)
                    performAtomicOperation(op, value, index, expected_value);
                    
                    // Then transition to Modified state after atomic write completes
                    cache[index].state = State::Modified;
                }
                
                cout << "CPU - " << id << ": Atomic operation completed | value: 0x" << hex << cache[index].value << dec 
                     << " | final state: " << stateToString(cache[index].state) << endl;
                cout << "<<< CPU - " << id << ": RELEASED BUS LOCK\n" << endl;
                break;
            } 
        }
    }

    BusResponse send_bus_operation(const BusOp& op, const int& address, const int& initiator_id);

};   // End of Processor class.

// Bus class - manages all processors and bus operations
class Bus {
private:
    mutex bus_mutex;  // Protect Bus operations from concurrent access
    
public:
    array<Processor, NUM_PROCESSORS> processors;
    
    // Get mutex for external synchronization if needed
    mutex& getMutex() { return bus_mutex; }
    
    Bus() {
        // Initialize processors with reference to this bus
        for (int i = 0; i < NUM_PROCESSORS; i++) {
            processors[i] = Processor(i, this);
        }
    }
    
    BusResponse broadcastBusOperation(const BusOp& op, const int& address, const int& initiator_id) {
        // Note: Lock is already held by calling cpu_operation()
        
        // Special handling for BusWB: The initiator writes back its own cache line to memory
        if (op == BusOp::BusWB) {
            memory[address] = processors[initiator_id].cache[(address / 4) % CACHE_SIZE].value;
            cout << "CPU - " << initiator_id << ": Write-back completed to memory | address: 0x" << hex << address 
                 << " | data: 0x" << hex << processors[initiator_id].cache[(address / 4) % CACHE_SIZE].value << dec << endl;
            BusResponse response;
            return response;
        }

        BusResponse response;
        response.data = memory[address];  // Default to memory data
        response.data_from_memory = true;
        response.requester_new_state = State::Invalid;
        response.state_changed = false;
        response.present_state = State::Invalid;
        response.core_id = -1;  // -1 indicates data from memory

        // Track if we found any sharers
        // Priority order: Modified > Owned > Exclusive > Shared > Memory
        // This ensures we always get the most up-to-date data
        bool found_sharer = false;
        bool found_exclusive = false;
        bool found_modified = false;
        bool found_owned = false;

        // Send bus operation to all other processors.
        for (int i = 0; i < NUM_PROCESSORS; i++) {   
            if (i == initiator_id) continue;    // Skip the initiator.
            
            Processor& other_processor = processors[i];
            int cache_index = (address / 4) % CACHE_SIZE;  // Calculate cache index
            CacheLine& other_cache_line = other_processor.cache[cache_index];

            // Check if this cache line actually contains the requested address
            bool address_match = (other_cache_line.address == address);

            switch (op) {
            case BusOp::BusRd: // Read request from initiator (P_i)
                // Priority order: Modified > Owned > Exclusive > Shared > Invalid
                // Only respond if address matches AND state is valid
                
                // Modified (highest priority) - always overwrites response
                if (other_cache_line.state == State::Modified && address_match) {
                    found_modified = true;
                    response.data = other_cache_line.value;
                    response.data_from_memory = false;
                    response.state_changed = true;
                    response.requester_new_state = State::Owned;
                    response.present_state = State::Modified;
                    response.core_id = i;
                    cout << "CPU - " << i << ": Snooped Cache-HIT @ addr 0x" << hex << address << dec << " (index " << cache_index << ") | state: " << stateToString(State::Modified) << endl;
                    cout << "CPU - " << i << ": Snooped Cache-Line Transition | [" << stateToString(State::Modified) 
                         << "->" << stateToString(State::Owned) << "]" << endl;
                    other_cache_line.state = State::Owned;
                } 
                // Owned - second priority, only if no Modified found
                else if (other_cache_line.state == State::Owned && address_match) {
                    found_owned = true;
                    if (!found_modified) {
                        response.data = other_cache_line.value;
                        response.data_from_memory = false;
                        response.requester_new_state = State::Owned;
                        response.state_changed = false;
                        response.present_state = State::Owned;
                        response.core_id = i;
                    }
                    cout << "CPU - " << i << ": Snooped Cache-HIT @ addr 0x" << hex << address << dec << " (index " << cache_index << ") | state: " << stateToString(State::Owned) << endl;
                    other_cache_line.state = State::Owned;
                } 
                // Exclusive - third priority, only if no Modified or Owned found
                else if (other_cache_line.state == State::Exclusive && address_match) {
                    found_exclusive = true;
                    if (!found_modified && !found_owned) {
                        // Exclusive state: Data is consistent with memory, read from memory
                        response.data = other_cache_line.value;
                        response.data_from_memory = false;
                        response.state_changed = true;
                        response.requester_new_state = State::Shared;
                        response.present_state = State::Exclusive;
                        response.core_id = -1;  // Data from memory
                        cout << "CPU - " << i << ": Snooped Cache-HIT @ addr 0x" << hex << address << dec << " (index " << cache_index << ") | state: " << stateToString(State::Exclusive) << endl;
                        cout << "CPU - " << i << ": Snooped Cache-Line Transition | [" << stateToString(State::Exclusive) 
                             << "->" << stateToString(State::Shared) << "]" << endl;
                    }
                    other_cache_line.state = State::Shared;
                } 
                // Shared - fourth priority, only if no cache data found yet
                else if (other_cache_line.state == State::Shared && address_match) {
                    found_sharer = true;
                    if (!found_modified && !found_owned && !found_exclusive) {
                        // Shared state: Check if any cache has Owned state to determine data source
                        // If no Owned cache exists, memory is up to date
                        // If Owned exists, data will come from that cache (higher priority already handled)
                        response.data = memory[address];
                        response.data_from_memory = true;
                        response.requester_new_state = State::Shared;
                        response.state_changed = false;
                        response.present_state = State::Shared;
                        response.core_id = -1;  // Data from memory
                    }
                    cout << "CPU - " << i << ": Snooped Cache-HIT @ addr 0x" << hex << address << dec << " (index " << cache_index << ") | state: " << stateToString(State::Shared) << endl;
                } 
                // Invalid (lowest priority) - no action needed
                else if (other_cache_line.state == State::Invalid && address_match) {
                    // No action needed
                }
                break;
            case BusOp::BusRdX: // Read-for-Ownership request from initiator (P_i)
                // Send data back to requester when snooped line is in M or O state
                // Invalidate for all other states
                
                if (other_cache_line.state == State::Modified && address_match) {
                    // Modified: Send data back, invalidate this cache line
                    found_modified = true;
                    response.data = other_cache_line.value;
                    response.data_from_memory = false;
                    response.state_changed = true;
                    response.requester_new_state = State::Modified;
                    response.present_state = State::Modified;
                    response.core_id = i;
                    cout << "CPU - " << i << ": Snooped Cache-HIT @ addr 0x" << hex << address << dec << " (index " << cache_index << ") | state: " << stateToString(State::Modified) << endl;
                    cout << "CPU - " << i << ": Snooped Cache-Line Transition | [" << stateToString(State::Modified) 
                         << "->" << stateToString(State::Invalid) << "]" << endl;
                    other_cache_line.state = State::Invalid;
                }
                else if (other_cache_line.state == State::Owned && address_match) {
                    // Owned: Send data back, invalidate this cache line
                    found_owned = true;
                    if (!found_modified) {
                        response.data = other_cache_line.value;
                        response.data_from_memory = false;
                        response.state_changed = true;
                        response.requester_new_state = State::Modified;
                        response.present_state = State::Owned;
                        response.core_id = i;
                    }
                    cout << "CPU - " << i << ": Snooped Cache-HIT @ addr 0x" << hex << address << dec << " (index " << cache_index << ") | state: " << stateToString(State::Owned) << endl;
                    cout << "CPU - " << i << ": Snooped Cache-Line Transition | [" << stateToString(State::Owned) 
                         << "->" << stateToString(State::Invalid) << "]" << endl;
                    other_cache_line.state = State::Invalid;
                }
                else if (other_cache_line.state == State::Exclusive && address_match) {
                    // Exclusive: Forward data and invalidate this cache line
                    found_exclusive = true;
                    if (!found_modified && !found_owned) {
                    response.data = other_cache_line.value;
                    response.data_from_memory = false;
                        response.state_changed = true;
                        response.requester_new_state = State::Modified;
                        response.present_state = State::Exclusive;
                        response.core_id = i;
                    }
                    cout << "CPU - " << i << ": Snooped Cache-HIT @ addr 0x" << hex << address << dec << " (index " << cache_index << ") | state: " << stateToString(State::Exclusive) << endl;
                    cout << "CPU - " << i << ": Snooped Cache-Line Transition | [" << stateToString(State::Exclusive) 
                         << "->" << stateToString(State::Invalid) << "]" << endl;
                    other_cache_line.state = State::Invalid;
                }
                else if (other_cache_line.state == State::Shared && address_match) {
                    // Shared: Invalidate this cache line
                    found_sharer = true;
                    if (!found_modified && !found_owned && !found_exclusive) {
                        response.data = memory[address];
                        response.data_from_memory = true;
                        response.state_changed = true;
                        response.requester_new_state = State::Modified;
                        response.present_state = State::Shared;
                        response.core_id = i;
                    }
                    cout << "CPU - " << i << ": Snooped Cache-HIT @ addr 0x" << hex << address << dec << " (index " << cache_index << ") | state: " << stateToString(State::Shared) << endl;
                    cout << "CPU - " << i << ": Snooped Cache-Line Transition | [" << stateToString(State::Shared) 
                         << "->" << stateToString(State::Invalid) << "]" << endl;
                    other_cache_line.state = State::Invalid;
                }
                // Invalid: No action needed
                break;
            case BusOp::BusUpgr: // Upgrade request from initiator (P_i)
                // BusUpgr: Invalidate all other copies, no data transfer needed
                
                if (other_cache_line.state == State::Modified && address_match) {
                    // Modified: Should not happen with BusUpgr (requester already has Shared)
                    cout << "CPU - " << i << ": Snooped Cache-HIT @ addr 0x" << hex << address << dec << " (index " << cache_index << ") | state: " << stateToString(State::Modified) << endl;
                    cout << "CPU - " << i << ": Snooped Cache-Line Transition | [" << stateToString(State::Modified) 
                         << "->" << stateToString(State::Invalid) << "]" << endl;
                    other_cache_line.state = State::Invalid;
                }
                else if (other_cache_line.state == State::Owned && address_match) {
                    // Owned: Invalidate this cache line
                    cout << "CPU - " << i << ": Snooped Cache-HIT @ addr 0x" << hex << address << dec << " (index " << cache_index << ") | state: " << stateToString(State::Owned) << endl;
                    cout << "CPU - " << i << ": Snooped Cache-Line Transition | [" << stateToString(State::Owned) 
                         << "->" << stateToString(State::Invalid) << "]" << endl;
                    other_cache_line.state = State::Invalid;
                }
                else if (other_cache_line.state == State::Exclusive && address_match) {
                    // Exclusive: Invalidate this cache line
                    cout << "CPU - " << i << ": Snooped Cache-HIT @ addr 0x" << hex << address << dec << " (index " << cache_index << ") | state: " << stateToString(State::Exclusive) << endl;
                    cout << "CPU - " << i << ": Snooped Cache-Line Transition | [" << stateToString(State::Exclusive) 
                         << "->" << stateToString(State::Invalid) << "]" << endl;
                    other_cache_line.state = State::Invalid;
                }
                else if (other_cache_line.state == State::Shared && address_match) {
                    // Shared: Invalidate this cache line
                    cout << "CPU - " << i << ": Snooped Cache-HIT @ addr 0x" << hex << address << dec << " (index " << cache_index << ") | state: " << stateToString(State::Shared) << endl;
                    cout << "CPU - " << i << ": Snooped Cache-Line Transition | [" << stateToString(State::Shared) 
                         << "->" << stateToString(State::Invalid) << "]" << endl;
                    other_cache_line.state = State::Invalid;
                }
                // Invalid: No action needed
                break;
            case BusOp::None:
                
                break;
            }

        }   // End of for loop.
        
        // Set the final requester_new_state for the initiator based on snoop results
        if (op == BusOp::BusRd) {
            // BusRd: Read request
            if (response.data_from_memory && !found_sharer && !found_exclusive && !found_modified && !found_owned) {
                // No sharers found, data from memory -> Exclusive state
                response.requester_new_state = State::Exclusive;
            } else if (found_sharer || found_exclusive || found_modified || found_owned) {
                // Data from cache or sharers exist -> Shared state
                // Note: In MOESI, Shared state may be dirty if there's an Owned copy
                // Priority handling (Modified/Owned) ensures correct data source
                response.requester_new_state = State::Shared;
            }
        } else if (op == BusOp::BusRdX) {
            // BusRdX: Read-for-Ownership request
            if (found_modified || found_owned) {
                // Data from Modified/Owned cache -> Modified state
                response.requester_new_state = State::Modified;
            } else {
                // No data from cache, data from memory -> Modified state
                response.requester_new_state = State::Modified;
                response.data = memory[address];
                response.data_from_memory = true;
                response.core_id = -1;
            }
        }
        
        return response;
    }
};

// Implementation of Processor::send_bus_operation
BusResponse Processor::send_bus_operation(const BusOp& op, const int& address, const int& initiator_id) {
    // Note: Lock is already held by calling cpu_operation()
    return bus->broadcastBusOperation(op, address, initiator_id);
}

// Function to generate a random address within the memory bounds
int addr_gen (array<int, MEMORY_SIZE>& mem) {
    random_device rd;  // obtain a random number from hardware
    mt19937 eng(rd()); // seed the generator
    uniform_int_distribution<> distr(0, mem.size() - 1); // define the range

    int random_index = (distr(eng) * 4) / 4;
    return random_index;
}

// ============================================
// TEST FUNCTIONS
// ============================================

void runReadWriteTest(Bus& bus) {
    // Initialize memory with test data
    memory[4] = 0x1111;
    memory[8] = 0x2222;
    memory[12] = 0x3333;
    memory[16] = 0x4444;
    memory[20] = 0x5555;
    memory[100] = 0xABCD;
    memory[200] = 0x1000;
    memory[204] = 0x2000;
    memory[208] = 0x3000;
    memory[260] = 0xAAAA;  // For conflict miss test (0x104 = 260)
    memory[300] = 0xBBBB;
    memory[400] = 0xCCCC;
    memory[500] = 0xDDDD;
    memory[600] = 0xEEEE;
    
    cout << "\n=== MOESI Cache Coherence Protocol Test ===\n\n";
    
    // Test 1: CPU-2 and CPU-3 both read the same address to create Shared state
    cout << "=== Test 1: Read same address from multiple CPUs (Shared state) ===\n";
    bus.processors[2].cpu_operation(CpuOp::Read, 4);
    bus.processors[3].cpu_operation(CpuOp::Read, 4);
    
    // Test 2: CPU-0 reads from Shared cache - data should come from memory
    cout << "\n=== Test 2: Read from already Shared cache ===\n";
    bus.processors[0].cpu_operation(CpuOp::Read, 4);
    
    // Test 2.5: Write to Shared cache - should trigger BusUpgr
    cout << "\n=== Test 2.5: Write to Shared cache (BusUpgr) ===\n";
    bus.processors[0].cpu_operation(CpuOp::Write, 4, 0x9999);
    
    // Test 3: Write operation should trigger BusRdX and invalidate Shared copies
    cout << "\n=== Test 3: Write operation (BusRdX) - invalidates Shared copies ===\n";
    bus.processors[1].cpu_operation(CpuOp::Write, 8, 0xABCD);
    
    // Test 4: Read from Modified cache - should get data from Modified cache, transition M->O
    cout << "\n=== Test 4: Read from Modified cache (M->O transition) ===\n";
    bus.processors[0].cpu_operation(CpuOp::Read, 4);
    
    // Test 5: Different address - Exclusive state
    cout << "\n=== Test 5: Read different address (Exclusive state) ===\n";
    bus.processors[2].cpu_operation(CpuOp::Read, 16);  // Read address 0x10 to get Exclusive
    
    // Test 5.5: Write to Exclusive cache - should transition E->M with no bus operation
    cout << "\n=== Test 5.5: Write to Exclusive cache (E->M transition) ===\n";
    bus.processors[2].cpu_operation(CpuOp::Write, 16, 0xDDDD);
    
    // Test 6: Write to different address
    cout << "\n=== Test 6: Write to different address ===\n";
    bus.processors[3].cpu_operation(CpuOp::Write, 12, 0x5678);
    
    // Test 7: Read back the written address
    cout << "\n=== Test 7: Read back written address ===\n";
    bus.processors[0].cpu_operation(CpuOp::Read, 12);
    
    // Test 8: Read from Modified cache to create Owned state in another cache
    cout << "\n=== Test 8: Read from Modified cache to create Owned state (M->O in another cache) ===\n";
    bus.processors[1].cpu_operation(CpuOp::Read, 4);
    
    // Test 9: Read again to check Owned state is maintained
    cout << "\n=== Test 9: Read again to maintain Owned state ===\n";
    bus.processors[2].cpu_operation(CpuOp::Read, 4);
    
    // Test 10: Write to Owned cache - should transition O->M with BusUpgr
    cout << "\n=== Test 10: Write to Owned cache (O->M transition with BusUpgr) ===\n";
    bus.processors[0].cpu_operation(CpuOp::Write, 4, 0xEEEE);
    
    // Test 11: Write to Modified cache - should remain Modified (M->M)
    cout << "\n=== Test 11: Write to Modified cache (M->M transition) ===\n";
    bus.processors[0].cpu_operation(CpuOp::Write, 4, 0xFFFF);
    
    // Test 12: Conflict miss with dirty data - should trigger BusWB (Read case)
    cout << "\n=== Test 12: Conflict miss with dirty data (BusWB - Read case) ===\n";
    // CPU-0 has address 0x4 at index 1 in Modified state
    // Access 0x104 which also maps to index 1, causing eviction of 0x4
    bus.processors[0].cpu_operation(CpuOp::Read, 0x104);  // This will cause conflict at index 1
    
    // Write to 0x104 to make it dirty
    bus.processors[0].cpu_operation(CpuOp::Write, 0x104, 0xBBBB);
    
    // Test 13: Conflict miss with dirty data - should trigger BusWB (Write case)
    cout << "\n=== Test 13: Conflict miss with dirty data (BusWB - Write case) ===\n";
    // Now access 0x4 again which maps to index 1, evicting 0x104 (dirty)
    bus.processors[0].cpu_operation(CpuOp::Write, 0x4, 0xCCCC);  // This will cause conflict at index 1
    
    // Test 14: Exclusive -> Invalid transition (E -> I)
    cout << "\n=== Test 14: Exclusive -> Invalid transition ===\n";
    bus.processors[1].cpu_operation(CpuOp::Read, 20);  // Get Exclusive state
    bus.processors[2].cpu_operation(CpuOp::Write, 20, 0x8888);  // Invalidates Exclusive copy (E→I)
    
    // Test 15: Owned -> Invalid transition (O -> I)
    cout << "\n=== Test 15: Owned -> Invalid transition ===\n";
    bus.processors[0].cpu_operation(CpuOp::Read, 8);  // Get Modified state
    bus.processors[1].cpu_operation(CpuOp::Read, 8);  // Creates Owned state
    bus.processors[2].cpu_operation(CpuOp::Write, 8, 0x6666);  // Invalidates Owned copy (O→I)
    
    // Test 16: Read-Modify-Write sequence
    cout << "\n=== Test 16: Read-Modify-Write sequence ===\n";
    bus.processors[0].cpu_operation(CpuOp::Read, 100);
    bus.processors[0].cpu_operation(CpuOp::Write, 100, 0xAAAA);
    bus.processors[0].cpu_operation(CpuOp::Read, 100);  // Verify read after write
    
    // Test 17: Multiple Exclusive states (different addresses)
    cout << "\n=== Test 17: Multiple Exclusive states ===\n";
    bus.processors[1].cpu_operation(CpuOp::Read, 200);
    bus.processors[2].cpu_operation(CpuOp::Read, 204);
    bus.processors[3].cpu_operation(CpuOp::Read, 208);  // All should be Exclusive
    
    // Test 18: Exclusive snoop behavior (E->S transition on snoop)
    cout << "\n=== Test 18: Exclusive snoop behavior (E->S) ===\n";
    bus.processors[0].cpu_operation(CpuOp::Read, 300);  // Get Exclusive
    bus.processors[1].cpu_operation(CpuOp::Read, 300);  // Should transition E→S in CPU-0
    
    // Test 19: Write to Exclusive snoop data
    cout << "\n=== Test 19: Write to Exclusive snoop data ===\n";
    bus.processors[2].cpu_operation(CpuOp::Read, 400);  // Get Exclusive
    bus.processors[3].cpu_operation(CpuOp::Write, 400, 0x5555);  // Should invalidate Exclusive copy
    
    // Test 20: Complex multi-core scenario
    cout << "\n=== Test 20: Complex multi-core scenario ===\n";
    bus.processors[0].cpu_operation(CpuOp::Write, 500, 0x6666);  // Get M
    bus.processors[1].cpu_operation(CpuOp::Read, 500);  // Get S (M→O in CPU-0)
    bus.processors[2].cpu_operation(CpuOp::Read, 500);  // Get S
    bus.processors[3].cpu_operation(CpuOp::Write, 500, 0x7777);  // Invalidate all (BusUpgr + BusRdX)
    
    // Test 21: Same address, different cores, sequential operations
    cout << "\n=== Test 21: Sequential operations on same address ===\n";
    bus.processors[0].cpu_operation(CpuOp::Read, 600);
    bus.processors[1].cpu_operation(CpuOp::Write, 600, 0x8888);
    bus.processors[2].cpu_operation(CpuOp::Read, 600);
    bus.processors[3].cpu_operation(CpuOp::Write, 600, 0x9999);
    bus.processors[0].cpu_operation(CpuOp::Read, 600);
}

// Atomic operations test: 4 threads incrementing a shared counter
void runAtomicADDTest(Bus& bus) {
    const int SHARED_COUNTER_ADDR = 1000;
    const int EXPECTED_FINAL_VALUE = 4;
    
    // Initialize shared counter to 0
    memory[SHARED_COUNTER_ADDR] = 0;
    
    cout << "\n=== ATOMIC OPERATIONS TEST ===\n";
    cout << "=== 4 threads (simulating 4 CPU cores) incrementing shared counter from 0 to 4 ===\n\n";
    cout << "Initial value: " << memory[SHARED_COUNTER_ADDR] << endl << endl;
    
    // Lambda function for thread to perform atomic increment
    auto incrementCounter = [&](int core_id) {
        bus.processors[core_id].cpu_operation(CpuOp::Atomic_ADD, SHARED_COUNTER_ADDR, 1);
    };
    
    // Create 4 threads, each running on a different core
    thread threads[NUM_PROCESSORS];
    
    cout << "Launching " << NUM_PROCESSORS << " threads to perform atomic increments...\n";
    
    // Launch all threads
    for (int i = 0; i < NUM_PROCESSORS; i++) {
        threads[i] = thread(incrementCounter, i);
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < NUM_PROCESSORS; i++) {
        threads[i].join();
    }
    
    // All threads have completed - check final result
    
    cout << "=== CACHE LINE STATE FOR ALL CORES ===\n";
    int cache_index = (SHARED_COUNTER_ADDR / 4) % CACHE_SIZE;
    for (int i = 0; i < NUM_PROCESSORS; i++) {
        cout << "CPU - " << i << ": Cache line " << cache_index 
             << " | address: 0x" << hex << bus.processors[i].cache[cache_index].address << dec
             << " | value: 0x" << hex << bus.processors[i].cache[cache_index].value << dec
             << " | state: " << stateToString(bus.processors[i].cache[cache_index].state) << endl;
    }
    
    cout << "\n=== FINAL RESULT ===\n";
    cout << "Expected final value: " << EXPECTED_FINAL_VALUE << endl;
    
    // Find the cache line in Modified state and check its value
    int final_value = 0;
    bool found_modified = false;
    for (int i = 0; i < NUM_PROCESSORS; i++) {
        int index = (SHARED_COUNTER_ADDR / 4) % CACHE_SIZE;
        if (bus.processors[i].cache[index].address == SHARED_COUNTER_ADDR && 
            bus.processors[i].cache[index].state == State::Modified) {
            final_value = bus.processors[i].cache[index].value;
            found_modified = true;
            cout << "Final value in Modified cache line (CPU-" << i << "): " << final_value << endl;
            break;
        }
    }
    
    if (!found_modified) {
        cout << "ERROR: No cache line in Modified state found!" << endl;
    }
    
    cout << "\nAtomic ADD: Test " << (found_modified && final_value == EXPECTED_FINAL_VALUE ? "PASSED" : "FAILED") << endl;
}

// ============================================
// MAIN
// ============================================

int main() {
    // Create the bus (automatically initializes all processors)
    Bus bus;

    // Run the read-write test (test the basic read-write operations and cache coherence)
    runReadWriteTest(bus);
    
    // Run the atomic add test (4 threads incrementing a shared counter from 0 to 4)
    runAtomicADDTest(bus);

    return 0;
}

