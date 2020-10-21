#!/bin/bash

EXEC="/home/dzahka3/rodinia_3.1/openmp/backprop/backprop 50000000"
# PERF="perf stat -e task-clock,cycles,instructions,cache-references,cache-misses numactl -N 0 -m 0 $EXEC"

# echo $PERF
# $PERF
# echo numactl -N 0 -i 0,2 $EXEC
# numactl -N 0 -i 0,2 $EXEC

# echo numactl -N 0 -i 0,1 $EXEC
# numactl -N 0 -i 0,1 $EXEC

# echo numactl -N 0 -m 1 $EXEC
# numactl -N 0 -m 1 $EXEC

# echo numactl -N 0 -m 0 $EXEC
# numactl -N 0 -m 0 $EXEC

# echo numactl -N 0 -i 0,2 $EXEC
# numactl -N 0 -i 0,2 $EXEC

echo numactl -N 0 -m 2 $EXEC
numactl -N 0 -m 2 $EXEC
