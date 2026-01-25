#include "seqFunctions.h"
#include "parFunctions.h"

#include <iostream>
#include <fstream>
#include <string>
#include <cstdint>
#include <chrono>
#include <omp.h>


using namespace std;
using namespace chrono;

void printOrders(const vector<uint64_t> &orders){
    int orderIndex = 0;
    for(uint64_t order : orders){
        cout << "OrderIndex: " << orderIndex++ << " Value: " << order << "\n";
    }
}

bool isValidHexLine(const string& line) {
    if (line.empty()) return false;

    for (char c : line) {
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

uint64_t hexToUint64(const string& hexStr) {
    return stoull(hexStr, nullptr, 16);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <file_path> <freq>\n";
        return 1;
    }

    string filePath = argv[1];
    uint32_t freq = stoul(argv[2]);

    // input file stream
    /*
    Equivalent to FILE *f = fopen("input.txt", "r");
    */
    ifstream file(filePath);

    if (!file.is_open()) {
        cerr << "Error: Could not open file: " << filePath << "\n";
        return 1;
    }

    string line;
    int lineNumber = 0;
    vector <uint64_t> orders;

    while (getline(file, line)) {
        lineNumber++;

        if (!isValidHexLine(line)) {
            cerr << "Invalid hex at line " << lineNumber << ": " << line << "\n";
            continue;
        }

        uint64_t value = 0;
        try {
            value = hexToUint64(line);
        } catch (const exception& e) {
            cerr << "Conversion error at line " << lineNumber << ": " << e.what() << "\n";
            continue;
        }
        orders.push_back(value); 
    }

    file.close();

    
    // First we will update using sequential code
    auto start = high_resolution_clock::now();
    updateDisplay_Seq(orders, freq);
    auto end = high_resolution_clock::now();
    auto seqTime = duration_cast<microseconds>(end - start);

    // Second we will update using parallel code
    start = high_resolution_clock::now();
    updateDisplay_Par(orders, freq);
    end = high_resolution_clock::now();
    auto parTime = duration_cast<microseconds>(end - start);

    cout << "Sequential Time for updateDisplay: " << seqTime.count() << " microseconds\n";
    cout << "Parallel Time for updateDisplay: " << parTime.count() << " microseconds\n";
    cout << "Speedup for updateDisplay: " << (double)seqTime.count() / (double)parTime.count() << "\n";


     // First we will update using sequential code
    start = high_resolution_clock::now();
    int64_t check1 = totalAmountTraded_Seq(orders);
    end = high_resolution_clock::now();
    cout<< "Sequential Total Amount Traded: " << check1 << "\n";
    seqTime = duration_cast<microseconds>(end - start);

    // Second we will update using parallel code
    start = high_resolution_clock::now();
    int64_t check2  = totalAmountTraded_Par(orders);
    end = high_resolution_clock::now();
    cout<< "Parallel Total Amount Traded: " << check2 << "\n";
    parTime = duration_cast<microseconds>(end - start);


    cout<<"==========================================\n";
    if(check1 == check2){
        cout << "Sequential and Parallel results match!\n";
    }else{
        cout << "Sequential and Parallel results do NOT match!\n";
    }
    cout<<"==========================================\n";

    cout << "Sequential Time for totalAmountTraded: " << seqTime.count() << " microseconds\n";
    cout << "Parallel Time for totalAmountTraded: " << parTime.count() << " microseconds\n";
    cout << "Speedup for totalAmountTraded: " << (double)seqTime.count() / (double)parTime.count() << "\n";



     // First we will update using sequential code
    start = high_resolution_clock::now();
    printOrderStats_Seq(orders);
    end = high_resolution_clock::now();
    
    seqTime = duration_cast<microseconds>(end - start);

    // Second we will update using parallel code
    start = high_resolution_clock::now();
    printOrderStats_Par(orders);
    end = high_resolution_clock::now();
    
    parTime = duration_cast<microseconds>(end - start);


    cout << "Sequential Time for printOrderStats: " << seqTime.count() << " microseconds\n";
    cout << "Parallel Time for printOrderStats: " << parTime.count() << " microseconds\n";
    cout << "Speedup for printOrderStats: " << (double)seqTime.count() / (double)parTime.count() << "\n";


    // printOrders(orders);
    return 0;
}