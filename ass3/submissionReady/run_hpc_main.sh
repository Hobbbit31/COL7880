#!/bin/bash
# HPC launcher for MPI B&B solver
set -e

INPUT="${1:-input_report.txt}"
NP_ARG="${2:-}"
OUTPUT="${3:-par_output.txt}"

MODULE_NAME="${MODULE_NAME:-compiler/gcc/11.2/openmpi/4.1.4}"
BUILD_BEFORE_RUN="${BUILD_BEFORE_RUN:-1}"

# load MPI module
source /etc/profile.d/modules.sh 2>/dev/null || true
module purge 2>/dev/null || true
module load "$MODULE_NAME"

# avoid broken UCX / missing libcuda spam
export OMPI_MCA_pml=ob1
export OMPI_MCA_btl=self,vader,tcp
export OMPI_MCA_opal_warn_on_missing_libcuda=0

# figure out NP: arg > env NP > PBS_NODEFILE line count > 1
if [ -n "$NP_ARG" ]; then
    NP="$NP_ARG"
elif [ -n "${NP:-}" ]; then
    NP="$NP"
elif [ -n "${PBS_NODEFILE:-}" ] && [ -f "$PBS_NODEFILE" ]; then
    NP=$(wc -l < "$PBS_NODEFILE")
else
    NP=1
fi

echo "== HPC run =="
echo "module : $MODULE_NAME"
echo "input  : $INPUT"
echo "output : $OUTPUT"
echo "NP     : $NP"

if [ "$BUILD_BEFORE_RUN" = "1" ]; then
    if [ -f Makefile ]; then
        make clean || true
        make all
    else
        rm -f main
        mpic++ -O3 -std=c++17 -o main main.cpp
    fi
fi

if [ -n "${PBS_NODEFILE:-}" ] && [ -f "$PBS_NODEFILE" ]; then
    mpirun -np "$NP" --hostfile "$PBS_NODEFILE" ./main "$INPUT" "$OUTPUT"
else
    mpirun -np "$NP" ./main "$INPUT" "$OUTPUT"
fi

echo "== output =="
cat "$OUTPUT"
