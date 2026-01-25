#include "seqFunctions.h"
#include <vector>
#include <map>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cstdint>

// Renamed Struct
struct OrderBookEntry_Seq {
    uint32_t stockID;
    bool orderType; 
    uint8_t orderQty;
    uint8_t orderValue;
};

// Renamed Function: decodePacket -> decodePacket_Seq
// Updated return type to OrderBookEntry_Seq
OrderBookEntry_Seq decodePacket_Seq(uint64_t packet) {
    OrderBookEntry_Seq entry;
    entry.stockID = (packet >> 0) & 0xFFFFFFFF;
    entry.orderType = ((packet >> 32) & 0x1) == 1;
    entry.orderQty = (packet >> 33) & 0xFF;
    entry.orderValue = (packet >> 41) & 0xFF;
    
    return entry;
}

// Renamed Function: unstuffBits -> unstuffBits_Seq
uint64_t unstuffBits_Seq(uint64_t packet) {
    uint64_t result = 0;
    int outputBitPos = 0;
    int consecutiveOnes = 0;
    const int TARGET_BITS = 49;
    
    int i = 0;
    while (i < 64 && outputBitPos < TARGET_BITS) {
        bool bit = (packet >> i) & 0x1;
        
        if (bit) {
            consecutiveOnes++;
            result |= (1ULL << outputBitPos);
            outputBitPos++;
            
            if (consecutiveOnes == 5) {
                consecutiveOnes = 0;
                i++; 
                
            }
        } else {
            consecutiveOnes = 0;
            outputBitPos++;
        }
        i++;
    }
    
    return result;
}

// Renamed Struct
struct StockInfo_Seq {
    uint32_t stockID;
    uint8_t lastSellValue;
    uint8_t lastBuyValue;
    uint8_t minSellValue;
    uint8_t maxBuyValue;
    int64_t sumOrderValue;  
    int orderCount;         
    bool hasSellForMin;     
    bool hasBuyForMax;      
    
    // Updated Constructor name
    StockInfo_Seq() : lastSellValue(0), lastBuyValue(0), 
                  minSellValue(255), maxBuyValue(0),
                  sumOrderValue(0), orderCount(0),
                  hasSellForMin(false), hasBuyForMax(false) {}
    
    int getSpread() const {
        return abs((int)lastSellValue - (int)lastBuyValue);
    }
};

// Renamed Function: updateDisplay -> updateDisplay_Seq
void updateDisplay_Seq(const std::vector<uint64_t> &orderBook, int32_t freq) {
    std::map<uint32_t, StockInfo_Seq> stockMap;
    int snapCount = 0;
    
    for (size_t i = 0; i < orderBook.size(); i++) {
        // Updated calls to _Seq functions
        uint64_t unstuffed = unstuffBits_Seq(orderBook[i]);
        OrderBookEntry_Seq entry = decodePacket_Seq(unstuffed);
        
        StockInfo_Seq& stock = stockMap[entry.stockID];
        stock.stockID = entry.stockID;
        
        if (entry.orderType) {
            stock.lastSellValue = entry.orderValue;
            stock.hasSellForMin = true;
            if (entry.orderValue < stock.minSellValue) {
                stock.minSellValue = entry.orderValue;
            }
        } else {
            stock.lastBuyValue = entry.orderValue;
            stock.hasBuyForMax = true;
            if (entry.orderValue > stock.maxBuyValue) {
                stock.maxBuyValue = entry.orderValue;
            }
        }
        
        if ((i + 1) % freq == 0 || i == orderBook.size() - 1) {
            std::vector<StockInfo_Seq> stocks;
            for (auto& pair : stockMap) {
                stocks.push_back(pair.second);
            }
            
            // Updated Lambda arguments to StockInfo_Seq
            std::sort(stocks.begin(), stocks.end(), [](const StockInfo_Seq& a, const StockInfo_Seq& b) {
                int spreadA = a.getSpread();
                int spreadB = b.getSpread();
                if (spreadA != spreadB) {
                    return spreadA > spreadB;
                }
                return a.stockID > b.stockID;
            });
            
            std::string filename = "output/seq/snap_" + std::to_string(snapCount) + ".txt";
            std::ofstream outFile(filename);
            
            for (const auto& stock : stocks) {
                int spread = stock.getSpread();
                outFile << stock.stockID << " " 
                        << (int)stock.lastSellValue << " " 
                        << (int)stock.lastBuyValue << " " 
                        << spread << "\n";
            }
            
            outFile.close();
            snapCount++;
        }
    }
}

// Renamed Function: totalAmountTraded -> totalAmountTraded_Seq
int64_t totalAmountTraded_Seq(const std::vector<uint64_t> &orderBook) {
    int64_t total = 0;
    
    for (uint64_t packet : orderBook) {
        // Updated calls to _Seq functions
        uint64_t unstuffed = unstuffBits_Seq(packet);
        OrderBookEntry_Seq entry = decodePacket_Seq(unstuffed);
        
        total += (int64_t)entry.orderQty * (int64_t)entry.orderValue;
    }
    
    return total;
}

// Renamed Function: printOrderStats -> printOrderStats_Seq
void printOrderStats_Seq(const std::vector<uint64_t> &orderBook) {
    std::map<uint32_t, StockInfo_Seq> stockMap;
    
    for (uint64_t packet : orderBook) {
        // Updated calls to _Seq functions
        uint64_t unstuffed = unstuffBits_Seq(packet);
        OrderBookEntry_Seq entry = decodePacket_Seq(unstuffed);
        
        StockInfo_Seq& stock = stockMap[entry.stockID];
        stock.stockID = entry.stockID;
        
        if (entry.orderType) {
            stock.hasSellForMin = true;
            if (entry.orderValue < stock.minSellValue) {
                stock.minSellValue = entry.orderValue;
            }
        } else {
            stock.hasBuyForMax = true;
            if (entry.orderValue > stock.maxBuyValue) {
                stock.maxBuyValue = entry.orderValue;
            }
        }
        
        stock.sumOrderValue += (int64_t)entry.orderValue;
        stock.orderCount++;
    }
    
    std::ofstream outFile("output/seq1/stats.txt");
    outFile << std::fixed << std::setprecision(4);
    
    for (auto& pair : stockMap) {
        const StockInfo_Seq& stock = pair.second;
            
        double avgOrderValue = 0.0;
        if (stock.orderCount > 0) {
            avgOrderValue = static_cast<double>(stock.sumOrderValue) /
                            static_cast<double>(stock.orderCount);
        }
            
        uint8_t minSell = stock.hasSellForMin ? stock.minSellValue : 0;
        uint8_t maxBuy  = stock.hasBuyForMax  ? stock.maxBuyValue  : 0;
    
        outFile << stock.stockID << " "
                << static_cast<int>(minSell) << " "
                << static_cast<int>(maxBuy) << " "
                << std::fixed << std::setprecision(3)
                << avgOrderValue << "\n";
    }
    
    outFile.close();
}