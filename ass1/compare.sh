#!/bin/bash

# Comparison script for parallel vs sequential implementations

# Default values
NUM_ORDERS=${1:-1000000}
NUM_STOCKS=${2:-10000}
SNAPSHOT_FREQ=${3:-100000}

echo "=============================================="
echo "  PARALLEL vs SEQUENTIAL COMPARISON"
echo "=============================================="
echo "Orders:         $NUM_ORDERS"
echo "Stocks:         $NUM_STOCKS"
echo "Snapshot Freq:  $SNAPSHOT_FREQ"
echo "=============================================="
echo ""

# Check if binaries exist, compile if needed
if [ ! -f "./test_seq" ] || [ ! -f "./test_par" ]; then
    echo "Binaries not found. Compiling..."
    g++ -std=c++17 -O3 -fopenmp -o test_seq test.cpp functions_seq.cpp
    g++ -std=c++17 -O3 -fopenmp -o test_par test.cpp functions_par.cpp
    if [ $? -ne 0 ]; then
        echo "Error: Compilation failed."
        exit 1
    fi
    echo ""
fi

# Create results directory
mkdir -p results

# Output file
RESULTS_FILE="results/comparison_$(date +%Y%m%d_%H%M%S).txt"

echo "Results will be saved to: $RESULTS_FILE"
echo ""

# Function to extract time from output
extract_time() {
    echo "$1" | grep "Average time" | head -1 | sed 's/.*: //' | awk '{print $1}'
}

# Header for results file
{
    echo "=============================================="
    echo "  PARALLEL vs SEQUENTIAL COMPARISON"
    echo "=============================================="
    echo "Date: $(date)"
    echo "Orders: $NUM_ORDERS"
    echo "Stocks: $NUM_STOCKS"
    echo "Snapshot Freq: $SNAPSHOT_FREQ"
    echo "=============================================="
    echo ""
} > "$RESULTS_FILE"

# ==================== RUN SEQUENTIAL VERSION ====================
echo "----------------------------------------------"
echo "Running SEQUENTIAL version (test_seq)..."
echo "----------------------------------------------"

OUTPUT=$(./test_seq performance -n "$NUM_ORDERS" -s "$NUM_STOCKS" -f "$SNAPSHOT_FREQ" 2>&1)

# Extract timing - get all 3 "Average time" lines
SEQ_TIME1=$(echo "$OUTPUT" | grep "Average time" | sed -n '1p' | sed 's/.*: //' | awk '{print $1}')
SEQ_TIME2=$(echo "$OUTPUT" | grep "Average time" | sed -n '2p' | sed 's/.*: //' | awk '{print $1}')
SEQ_TIME3=$(echo "$OUTPUT" | grep "Average time" | sed -n '3p' | sed 's/.*: //' | awk '{print $1}')

echo "  totalAmountTraded: ${SEQ_TIME1} ms"
echo "  printOrderStats:   ${SEQ_TIME2} ms"
echo "  updateDisplay:     ${SEQ_TIME3} ms"
echo ""

{
    echo "SEQUENTIAL (test_seq):"
    echo "  totalAmountTraded: ${SEQ_TIME1} ms"
    echo "  printOrderStats:   ${SEQ_TIME2} ms"
    echo "  updateDisplay:     ${SEQ_TIME3} ms"
    echo ""
} >> "$RESULTS_FILE"

# ==================== RUN PARALLEL VERSION WITH DIFFERENT THREAD COUNTS ====================
THREAD_COUNTS="1 2 4 8 10"

# Store parallel results in temp files
for threads in $THREAD_COUNTS; do
    echo "----------------------------------------------"
    echo "Running PARALLEL version (test_par) with $threads thread(s)..."
    echo "----------------------------------------------"

    OUTPUT=$(OMP_NUM_THREADS=$threads ./test_par performance -n "$NUM_ORDERS" -s "$NUM_STOCKS" -f "$SNAPSHOT_FREQ" 2>&1)

    TIME1=$(echo "$OUTPUT" | grep "Average time" | sed -n '1p' | sed 's/.*: //' | awk '{print $1}')
    TIME2=$(echo "$OUTPUT" | grep "Average time" | sed -n '2p' | sed 's/.*: //' | awk '{print $1}')
    TIME3=$(echo "$OUTPUT" | grep "Average time" | sed -n '3p' | sed 's/.*: //' | awk '{print $1}')

    # Save to temp file
    echo "$TIME1 $TIME2 $TIME3" > "/tmp/par_${threads}.txt"

    echo "  totalAmountTraded: ${TIME1} ms"
    echo "  printOrderStats:   ${TIME2} ms"
    echo "  updateDisplay:     ${TIME3} ms"
    echo ""

    {
        echo "PARALLEL (test_par) - $threads thread(s):"
        echo "  totalAmountTraded: ${TIME1} ms"
        echo "  printOrderStats:   ${TIME2} ms"
        echo "  updateDisplay:     ${TIME3} ms"
        echo ""
    } >> "$RESULTS_FILE"
done

# ==================== SPEEDUP SUMMARY ====================
echo "=============================================="
echo "  SPEEDUP SUMMARY (vs Sequential)"
echo "=============================================="
echo ""

{
    echo "=============================================="
    echo "  SPEEDUP SUMMARY (vs Sequential)"
    echo "=============================================="
    echo ""
} >> "$RESULTS_FILE"

printf "%-15s | %-22s | %-22s | %-22s\n" "Version" "totalAmountTraded" "printOrderStats" "updateDisplay"
printf "%-15s | %-22s | %-22s | %-22s\n" "---------------" "----------------------" "----------------------" "----------------------"

{
    printf "%-15s | %-22s | %-22s | %-22s\n" "Version" "totalAmountTraded" "printOrderStats" "updateDisplay"
    printf "%-15s | %-22s | %-22s | %-22s\n" "---------------" "----------------------" "----------------------" "----------------------"
} >> "$RESULTS_FILE"

# Print sequential baseline
printf "%-15s | %-22s | %-22s | %-22s\n" "Sequential" "${SEQ_TIME1} ms (1.00x)" "${SEQ_TIME2} ms (1.00x)" "${SEQ_TIME3} ms (1.00x)"
printf "%-15s | %-22s | %-22s | %-22s\n" "Sequential" "${SEQ_TIME1} ms (1.00x)" "${SEQ_TIME2} ms (1.00x)" "${SEQ_TIME3} ms (1.00x)" >> "$RESULTS_FILE"

# Print parallel results with speedups
for threads in $THREAD_COUNTS; do
    read TIME1 TIME2 TIME3 < "/tmp/par_${threads}.txt"

    # Calculate speedups using awk (more portable than bc)
    SPEEDUP1=$(awk "BEGIN {printf \"%.2f\", $SEQ_TIME1 / $TIME1}" 2>/dev/null || echo "N/A")
    SPEEDUP2=$(awk "BEGIN {printf \"%.2f\", $SEQ_TIME2 / $TIME2}" 2>/dev/null || echo "N/A")
    SPEEDUP3=$(awk "BEGIN {printf \"%.2f\", $SEQ_TIME3 / $TIME3}" 2>/dev/null || echo "N/A")

    printf "%-15s | %-22s | %-22s | %-22s\n" "Parallel-${threads}T" "${TIME1} ms (${SPEEDUP1}x)" "${TIME2} ms (${SPEEDUP2}x)" "${TIME3} ms (${SPEEDUP3}x)"
    printf "%-15s | %-22s | %-22s | %-22s\n" "Parallel-${threads}T" "${TIME1} ms (${SPEEDUP1}x)" "${TIME2} ms (${SPEEDUP2}x)" "${TIME3} ms (${SPEEDUP3}x)" >> "$RESULTS_FILE"

    rm -f "/tmp/par_${threads}.txt"
done

echo ""
echo "=============================================="
echo "  COMPARISON COMPLETE"
echo "=============================================="
echo "Results saved to: $RESULTS_FILE"
echo ""
echo "Note: Speedup = Sequential Time / Parallel Time"
echo "      Speedup > 1 means parallel is faster"
echo "=============================================="

{
    echo ""
    echo "=============================================="
    echo "  COMPARISON COMPLETE"
    echo "=============================================="
    echo "Note: Speedup = Sequential Time / Parallel Time"
    echo "      Speedup > 1 means parallel is faster"
} >> "$RESULTS_FILE"
