#!/bin/bash

# Default values
NUM_ORDERS=${1:-50000000}
NUM_STOCKS=${2:-100000}
SNAPSHOT_FREQ=${3:-100000}

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo "=============================================="
echo "  TEST CASES BENCHMARK"
echo "=============================================="
echo "Orders:         $NUM_ORDERS"
echo "Stocks:         $NUM_STOCKS"
echo "Snapshot Freq:  $SNAPSHOT_FREQ"
echo "=============================================="

# 1. COMPILE WITH OPTIMIZATION (-O3)
echo -n "Compiling..."
g++ -std=c++17 -O3 -march=native -fopenmp -o test_seq test_seq.cpp functions_sequential.cpp
if [ $? -ne 0 ]; then echo -e "${RED}Failed to compile sequential.${NC}"; exit 1; fi

g++ -std=c++17 -O3 -march=native -fopenmp -o test_par test_par.cpp functions.cpp
if [ $? -ne 0 ]; then echo -e "${RED}Failed to compile parallel.${NC}"; exit 1; fi
echo -e "${GREEN}Done (with -O3 optimization).${NC}"

rm -rf results testPar testSeq
# Create directories
mkdir -p results testPar testSeq

RESULTS_FILE="results/comparison_$(date +%Y%m%d_%H%M%S).txt"

# Header for File
{
    echo "=============================================="
    echo "  PARALLEL vs SEQUENTIAL COMPARISON (-O3)"
    echo "=============================================="
    echo "Orders: $NUM_ORDERS | Stocks: $NUM_STOCKS"
    echo "=============================================="
} > "$RESULTS_FILE"

# ==================== RUN SEQUENTIAL ====================
echo "----------------------------------------------"
echo "Running SEQUENTIAL version..."
OUTPUT=$(./test_seq performance -n "$NUM_ORDERS" -s "$NUM_STOCKS" -f "$SNAPSHOT_FREQ" 2>&1)

# Robust Extraction using [METRIC]
SEQ_TIME1=$(echo "$OUTPUT" | grep "\[METRIC\] totalAmountTraded" | awk '{print $3}')
SEQ_TIME2=$(echo "$OUTPUT" | grep "\[METRIC\] printOrderStats"   | awk '{print $3}')
SEQ_TIME3=$(echo "$OUTPUT" | grep "\[METRIC\] updateDisplay"     | awk '{print $3}')

# Validate Extraction
if [ -z "$SEQ_TIME1" ]; then SEQ_TIME1=1; fi
if [ -z "$SEQ_TIME2" ]; then SEQ_TIME2=1; fi
if [ -z "$SEQ_TIME3" ]; then SEQ_TIME3=1; fi

TOTAL_SEQ=$(echo "$SEQ_TIME1 + $SEQ_TIME2 + $SEQ_TIME3" | bc)

# --- NEW: PRINT SEQUENTIAL TABLE ---
echo -e "\n${CYAN}SEQUENTIAL BASELINE (1.0x)${NC}"
printf "%-20s | %-15s\n" "Function" "Time (ms)"
printf "%-20s | %-15s\n" "--------------------" "---------------"
printf "%-20s | %-15s\n" "totalAmountTraded" "${SEQ_TIME1}"
printf "%-20s | %-15s\n" "printOrderStats" "${SEQ_TIME2}"
printf "%-20s | %-15s\n" "updateDisplay" "${SEQ_TIME3}"
printf "%-20s | %-15s\n" "--------------------" "---------------"
printf "%-20s | %-15s\n" "TOTAL" "${TOTAL_SEQ}"

# Log to file
{
    echo "SEQUENTIAL BASELINE:"
    echo "  totalAmountTraded: ${SEQ_TIME1} ms"
    echo "  printOrderStats:   ${SEQ_TIME2} ms"
    echo "  updateDisplay:     ${SEQ_TIME3} ms"
    echo "  TOTAL:             ${TOTAL_SEQ} ms"
    echo ""
} >> "$RESULTS_FILE"

# ==================== RUN PARALLEL & VERIFY ====================
echo -e "\n${CYAN}PARALLEL PERFORMANCE${NC}"
THREAD_COUNTS="4 8 10 12 16"

printf "%-10s | %-25s | %-25s | %-25s | %-10s\n" "Threads" "TotalAmount" "OrderStats" "UpdateDisp" "Status"
printf "%-10s | %-25s | %-25s | %-25s | %-10s\n" "----------" "-------------------------" "-------------------------" "-------------------------" "----------"

for threads in $THREAD_COUNTS; do
    # Run Parallel
    OUTPUT=$(OMP_NUM_THREADS=$threads ./test_par performance -n "$NUM_ORDERS" -s "$NUM_STOCKS" -f "$SNAPSHOT_FREQ" 2>&1)

    # Robust Extraction
    TIME1=$(echo "$OUTPUT" | grep "\[METRIC\] totalAmountTraded" | awk '{print $3}')
    TIME2=$(echo "$OUTPUT" | grep "\[METRIC\] printOrderStats"   | awk '{print $3}')
    TIME3=$(echo "$OUTPUT" | grep "\[METRIC\] updateDisplay"     | awk '{print $3}')

    # Safety clamp for 0ms
    if [[ "$TIME1" == "0.0000" || -z "$TIME1" ]]; then TIME1=0.0001; fi
    if [[ "$TIME2" == "0.0000" || -z "$TIME2" ]]; then TIME2=0.0001; fi
    if [[ "$TIME3" == "0.0000" || -z "$TIME3" ]]; then TIME3=0.0001; fi

    # Speedups
    S1=$(echo "scale=2; $SEQ_TIME1 / $TIME1" | bc)
    S2=$(echo "scale=2; $SEQ_TIME2 / $TIME2" | bc)
    S3=$(echo "scale=2; $SEQ_TIME3 / $TIME3" | bc)

    # Verification
    python3 cmp.py > /dev/null 2>&1
    if [ $? -eq 0 ]; then
        STATUS="${GREEN}PASS${NC}"
        STATUS_TEXT="PASS"
    else
        STATUS="${RED}FAIL${NC}"
        STATUS_TEXT="FAIL"
    fi

    # Print Row
    printf "%-10s | %-25s | %-25s | %-25s | %-10b\n" \
        "${threads}T" "${S1}x (${TIME1}ms)" "${S2}x (${TIME2}ms)" "${S3}x (${TIME3}ms)" "${STATUS}"

    # Log to file
    {
        echo "PARALLEL ($threads Threads) - Validation: $STATUS_TEXT"
        echo "  totalAmountTraded: ${TIME1} ms (Speedup: ${S1}x)"
        echo "  printOrderStats:   ${TIME2} ms (Speedup: ${S2}x)"
        echo "  updateDisplay:     ${TIME3} ms (Speedup: ${S3}x)"
        echo ""
    } >> "$RESULTS_FILE"
done

echo ""
echo "Full detailed report saved to: $RESULTS_FILE"