#!/bin/bash

EXEC="/home/dzahka3/rodinia_3.1/openmp/lud/omp/lud_omp"
NTHREAD=2
SIZE=22000

# CMD="numactl -N 0 -m 0 $EXEC -s $SIZE -n $NTHREAD"
# echo $CMD
# $CMD

# CMD="numactl -N 0 -i 0,2 $EXEC -s $SIZE -n $NTHREAD"
# echo $CMD
# $CMD

CMD="numactl -N 0 -m 2 $EXEC -s $SIZE -n $NTHREAD"
echo $CMD
$CMD

