#!/bin/bash

# Load MPI module
module purge
module load compiler/gcc/11.2/openmpi/4.1.4 2>/dev/null || module load compiler/gcc/9.1/openmpi/4.1.2

# OpenMPI workarounds
export OMPI_MCA_pml=ob1
export OMPI_MCA_btl=self,vader,tcp
export OMPI_MCA_opal_warn_on_missing_libcuda=0

# Compile (main_combined.cpp as main.cpp since it's all-in-one)
echo "Compiling..."
mpic++ -O3 -std=c++17 main.cpp -o main
if [ $? -ne 0 ]; then
    echo "Compilation failed!"
    exit 1
fi
echo "Compilation successful."

INPUT="input_report.txt"
OUTPUT="output.txt"
RESULT="benchmark_results.md"

# Determine hostfile arg
HOSTFILE_ARG=""
if [ -n "$PBS_NODEFILE" ]; then
    HOSTFILE_ARG="--hostfile $PBS_NODEFILE"
fi

echo "# Benchmark Results" > "$RESULT"
echo "" >> "$RESULT"
echo "| Processes | Time (seconds) | Profit | Clique |" >> "$RESULT"
echo "|-----------|----------------|--------|--------|" >> "$RESULT"

for np in $(seq 1 16); do
    echo "Running with np=$np ..."
    TIME=$(mpirun -np "$np" $HOSTFILE_ARG ./main "$INPUT" "$OUTPUT" 2>/dev/null | grep "Execution time" | awk '{print $3}')
    if [ -z "$TIME" ]; then
        TIME="N/A"
    fi
    PROFIT=$(head -1 "$OUTPUT" 2>/dev/null)
    CLIQUE=$(tail -1 "$OUTPUT" 2>/dev/null)
    echo "| $np | $TIME | $PROFIT | $CLIQUE |" >> "$RESULT"
    echo "  np=$np -> ${TIME}s  profit=$PROFIT"
done

echo "" >> "$RESULT"
echo "Done! Results saved to $RESULT"
cat "$RESULT"
