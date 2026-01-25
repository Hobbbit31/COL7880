#include "functions.h"
using namespace std;
#include <omp.h>


// ==========================================
// INTERNAL HELPER STRUCTURES & FUNCTIONS
// ==========================================

struct OrderBookEntry {
    uint32_t stockID;
    bool orderType; // 0 = Buy, 1 = Sell
    uint8_t orderQty;
    uint8_t orderValue;
};

// Represents the running state of a single stock for Part 1
struct StockState {
    uint32_t stockID;
    uint8_t lastSell;
    uint8_t lastBuy;
    
    // Per Update 1: Parameters assumed 0 if no trade detected.
    // We initialize to 0.
    StockState() : stockID(0), lastSell(0), lastBuy(0) {}

    int getSpread() const {
        return std::abs((int)lastSell - (int)lastBuy);
    }
};

// Represents statistical data for Part 2
struct StockStats {
    uint32_t stockID;
    uint8_t minSell;
    uint8_t maxBuy;
    uint64_t totalValue;
    uint32_t count;
    bool hasSell; 
    bool hasBuy;  

    // We init minSell to 255 (max uint8) to easily find the true minimum.
    // The 'hasSell' flag ensures we print 0 if no sell actually happens (Update 1).
    StockStats() : stockID(0), minSell(255), maxBuy(0), 
                   totalValue(0), count(0), hasSell(false), hasBuy(false) {}
};

// Inline helper to unstuff bits (0 inserted after every 5 consecutive 1s) 
inline uint64_t unstuffBits(uint64_t packet) {
    uint64_t result = 0;
    int outputBitPos = 0;
    int consecutiveOnes = 0;
    const int TARGET_BITS = 49;
    
    // We iterate up to 64 to be safe, but break once we have our 49 bits.
    for (int i = 0; i < 64 && outputBitPos < TARGET_BITS; i++) {
        bool bit = (packet >> i) & 0x1;
        
        if (bit) {
            consecutiveOnes++;
            result |= (1ULL << outputBitPos);
            outputBitPos++;
            
            if (consecutiveOnes == 5) {
                // Skip the stuffed 0 (it corresponds to the next iteration of i)
                consecutiveOnes = 0;
                i++; 
            }
        } else {
            consecutiveOnes = 0;
            // A genuine 0 bit
            outputBitPos++;
        }
    }
    return result;
}

// Inline helper to decode the packet structure 
inline OrderBookEntry decodePacket(uint64_t unstuffed) {
    OrderBookEntry entry;
    // 1. StockID (32 bits)
    entry.stockID = (uint32_t)((unstuffed >> 0) & 0xFFFFFFFF);
    // 2. OrderType (1 bit)
    entry.orderType = ((unstuffed >> 32) & 0x1) == 1;
    // 3. OrderQty (8 bits)
    entry.orderQty = (uint8_t)((unstuffed >> 33) & 0xFF);
    // 4. OrderValue (8 bits)
    entry.orderValue = (uint8_t)((unstuffed >> 41) & 0xFF);
    return entry;
}

// ==========================================
// PART 1: UPDATE DISPLAY (Task-Based)
// ==========================================

void updateDisplay(const vector<uint64_t>& orderBook, int32_t freq) {
    // Strategy: Producer-Consumer
    // Main thread updates state sequentially (correctness).
    // When a snapshot is needed, it spawns a Task to sort/write (parallelism).
    
    // Map tracks the LATEST state of every stock
    std::map<uint32_t, StockState> currentMarketState;
    
    #pragma omp parallel
    {
        #pragma omp single
        {
            int snapCount = 0;
            size_t N = orderBook.size();

            for (size_t i = 0; i < N; i++) {
                // 1. Decode & Update State (Sequential)
                uint64_t raw = unstuffBits(orderBook[i]);
                OrderBookEntry entry = decodePacket(raw);

                StockState& stock = currentMarketState[entry.stockID];
                stock.stockID = entry.stockID; 
                
                if (entry.orderType) {
                    stock.lastSell = entry.orderValue;
                } else {
                    stock.lastBuy = entry.orderValue;
                }

                // 2. Check Snapshot Trigger 
                if ((i + 1) % freq == 0 || i == N - 1) {
                    
                    // Create a snapshot vector (Deep Copy)
                    // We must copy because the main loop will continue modifying 'currentMarketState'
                    vector<StockState> snapshotData;
                    snapshotData.reserve(currentMarketState.size());
                    for (const auto& pair : currentMarketState) {
                        snapshotData.push_back(pair.second);
                    }

                    // 3. Spawn Background Task
                    // 'firstprivate' ensures the task gets the copy of data and snapCount
                    #pragma omp task firstprivate(snapshotData, snapCount)
                    {
                        // Sort: Decreasing Spread -> Larger StockID 
                        std::sort(snapshotData.begin(), snapshotData.end(), 
                            [](const StockState& a, const StockState& b) {
                                int spreadA = a.getSpread();
                                int spreadB = b.getSpread();
                                if (spreadA != spreadB) {
                                    return spreadA > spreadB; 
                                }
                                return a.stockID > b.stockID; 
                        });

                        // Write File
                        string filename = "output/par/snap_" + std::to_string(snapCount) + ".txt";
                        std::ofstream outFile(filename);
                        for (const auto& s : snapshotData) {
                            outFile << s.stockID << " " 
                                    << (int)s.lastSell << " " 
                                    << (int)s.lastBuy << " " 
                                    << s.getSpread() << "\n";
                        }
                        outFile.close();
                    }
                    
                    snapCount++;
                }
            }
        } 
        // Implicit barrier: Function won't return until all Tasks are done.
    }
}

// ==========================================
// PART 2A: TOTAL AMOUNT TRADED
// ==========================================

int64_t totalAmountTraded(const vector<uint64_t>& orderBook) {
    int64_t total = 0;
    size_t N = orderBook.size();

    // Simple Parallel Reduction 
    #pragma omp parallel for reduction(+:total)
    for (size_t i = 0; i < N; i++) {
        uint64_t raw = unstuffBits(orderBook[i]);
        OrderBookEntry entry = decodePacket(raw);
        
        total += ((int64_t)entry.orderQty * (int64_t)entry.orderValue);
    }

    return total;
}

// ==========================================
// PART 2B: ORDER BOOK STATISTICS
// ==========================================

void printOrderStats(const vector<uint64_t>& orderBook) {
    // Global map for final results
    std::map<uint32_t, StockStats> globalStats;

    #pragma omp parallel
    {
        // Thread-Local Map (Avoids lock contention)
        std::map<uint32_t, StockStats> localStats;

        // Dynamic schedule handles load imbalance if some packets take longer
        #pragma omp for schedule(dynamic) nowait
        for (size_t i = 0; i < orderBook.size(); i++) {
            uint64_t raw = unstuffBits(orderBook[i]);
            OrderBookEntry entry = decodePacket(raw);

            StockStats& stats = localStats[entry.stockID];
            stats.stockID = entry.stockID;

            if (entry.orderType) { // Sell
                stats.hasSell = true;
                if (entry.orderValue < stats.minSell) {
                    stats.minSell = entry.orderValue;
                }
            } else { // Buy
                stats.hasBuy = true;
                if (entry.orderValue > stats.maxBuy) {
                    stats.maxBuy = entry.orderValue;
                }
            }

            stats.totalValue += entry.orderValue;
            stats.count++;
        }

        // Merge Phase: Combine local maps into global map
        #pragma omp critical
        {
            for (const auto& pair : localStats) {
                const StockStats& local = pair.second;
                StockStats& global = globalStats[local.stockID];
                
                global.stockID = local.stockID;
                
                // Merge Min Sell
                if (local.hasSell) {
                    if (!global.hasSell || local.minSell < global.minSell) {
                        global.minSell = local.minSell;
                        global.hasSell = true;
                    }
                }
                
                // Merge Max Buy
                if (local.hasBuy) {
                    if (!global.hasBuy || local.maxBuy > global.maxBuy) {
                        global.maxBuy = local.maxBuy;
                        global.hasBuy = true;
                    }
                }

                // Accumulate Sum and Count
                global.totalValue += local.totalValue;
                global.count += local.count;
            }
        }
    }

    // Output Phase
    std::ofstream outFile("output/par/stats.txt");
    outFile << std::fixed << std::setprecision(4); // 4 decimal places 

    for (const auto& pair : globalStats) {
        const StockStats& s = pair.second;
        
        double avg = 0.0;
        if (s.count > 0) {
            avg = (double)s.totalValue / (double)s.count;
        }

        // Update 1: "All parameters... assumed to be 0, if no trade detected"
        // We check flags. If hasSell is false, minSell defaults to 0.
        int finalMinSell = s.hasSell ? (int)s.minSell : 0;
        int finalMaxBuy = s.hasBuy ? (int)s.maxBuy : 0;
        
        outFile << s.stockID << " " 
                << finalMinSell << " " 
                << finalMaxBuy << " " 
                << avg << "\n";
    }
    outFile.close();
}