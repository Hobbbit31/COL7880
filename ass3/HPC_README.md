# HPC Run Guide

This file is the practical runbook for running this assignment on the IITD PBS cluster.

Use it when you need to:

- start a fresh PBS job
- rebuild in the right MPI environment
- run the MPI solver
- change process count or module version
- debug common PBS / OpenMPI failures

## Recommended Flow

From the login node, request an interactive PBS job with 16 MPI ranks:

```bash
qsub -I -P col7880.mcs252151.course -N a3 -l select=1:ncpus=16:mpiprocs=16:ngpus=0
```

After the job starts, verify the allocation:

```bash
echo "$PBS_JOBID"
echo "$PBS_NODEFILE"
wc -l "$PBS_NODEFILE"
cat "$PBS_NODEFILE"
hostname
```

Expected pattern:

- `PBS_NODEFILE` exists
- `wc -l "$PBS_NODEFILE"` is `16`
- the node name is repeated `16` times

Then go to the assignment directory and use the launcher:

```bash
cd ~/main
./run_hpc_main.sh
```

## What `run_hpc_main.sh` Does

The launcher script:

- loads a known OpenMPI module
- applies the OpenMPI workaround that avoids the broken UCX path
- rebuilds with `make clean && make all`
- infers `NP` from `"$PBS_NODEFILE"` if you do not pass one
- runs `mpirun`
- prints the final output file

Default module:

```bash
compiler/gcc/11.2/openmpi/4.1.4
```

Default process count priority:

1. second positional argument
2. `NP` environment variable
3. number of lines in `"$PBS_NODEFILE"`
4. fallback to `1`

## Common Commands

Run with defaults:

```bash
./run_hpc_main.sh
```

Run with explicit input, rank count, and output:

```bash
./run_hpc_main.sh input_report.txt 16 par_output.txt
```

Use a different MPI module:

```bash
MODULE_NAME=compiler/gcc/9.1/openmpi/4.1.2 ./run_hpc_main.sh
```

Skip rebuild if you already compiled in the same module environment:

```bash
BUILD_BEFORE_RUN=0 ./run_hpc_main.sh
```

Use the script from a PBS job but force a smaller rank count:

```bash
./run_hpc_main.sh input_report.txt 8
```

## Manual Fallback

If you want to run everything manually:

```bash
module purge
module load compiler/gcc/11.2/openmpi/4.1.4
export OMPI_MCA_pml=ob1
export OMPI_MCA_btl=self,vader,tcp
export OMPI_MCA_opal_warn_on_missing_libcuda=0
make clean
make all
mpirun -np 16 --hostfile "$PBS_NODEFILE" ./main input_report.txt par_output.txt
cat par_output.txt
```

## Expected Output

For the current `input_report.txt`, the expected output is:

```text
650
4 44 46 134 142 152 244 299 321 344 347 353
```

## Trying Different MPI Modules

Preferred order to try:

1. `compiler/gcc/11.2/openmpi/4.1.4`
2. `compiler/gcc/9.1/openmpi/4.1.2`
3. `compiler/gcc/9.1/openmpi/4.1.6`

Avoid older OpenMPI versions unless required by the cluster or course setup.

Important rule:

- always load the module first
- always rebuild after changing the module
- do not trust an old `./main` binary built in a different MPI environment

## Troubleshooting

### Error: `There are not enough slots available`

Cause:

- PBS gave you fewer MPI slots than you requested
- or you launched outside the PBS allocation

Check:

```bash
wc -l "$PBS_NODEFILE"
cat "$PBS_NODEFILE"
```

Fix:

- request `mpiprocs=16` in `qsub`
- run inside the PBS job
- use `--hostfile "$PBS_NODEFILE"`

Correct request example:

```bash
qsub -I -P col7880.mcs252151.course -N a3 -l select=1:ncpus=16:mpiprocs=16:ngpus=0
```

### Error: `mpirun: command not found`

Cause:

- MPI module is not loaded

Fix:

```bash
module purge
module load compiler/gcc/11.2/openmpi/4.1.4
which mpirun
which mpic++
```

### UCX version mismatch or CUDA warning spam

Typical symptoms:

- `UCP version is incompatible`
- missing `libcuda.so.1`

Cause:

- OpenMPI is trying to use a UCX path that does not match the node runtime

Fix:

```bash
export OMPI_MCA_pml=ob1
export OMPI_MCA_btl=self,vader,tcp
export OMPI_MCA_opal_warn_on_missing_libcuda=0
```

The launcher already exports these values.

### Program runs but output is missing or wrong

Check:

```bash
cat par_output.txt
./seq_main input_report.txt seq_output.txt
cat seq_output.txt
diff -u seq_output.txt par_output.txt
```

If the sequential and parallel outputs differ, treat that as a code issue, not a PBS issue.

### You changed nodes or modules

Rebuild again:

```bash
make clean
make all
```

This matters because the MPI binary must match the `mpic++` and runtime libraries in the current environment.

### `PBS_NP` is empty

That is not fatal on this cluster.

Use `"$PBS_NODEFILE"` as the source of truth:

```bash
wc -l "$PBS_NODEFILE"
```

If the line count is `16`, you effectively have 16 slots.

## Alternate Variant

If you also want to test `root-greedy-balance`, repeat the same workflow in that directory:

```bash
cd ~/root-greedy-balance
./run_hpc_main.sh
```

If that directory does not contain the launcher yet, copy it there first.
