#!/bin/bash

EXEC="/home/dzahka3/rodinia_3.1/openmp/hotspot/hotspot"
F1="/home/dzahka3/rodinia_3.1/data/hotspot/temp_16384"
F2="/home/dzahka3/rodinia_3.1/data/hotspot/power_16384"
THREADS=2

# echo "numactl -N 0 -m 0 $EXEC 16384 16384 100 $THREADS $F1 $F2 /dev/null"
# numactl -N 0 -m 0 $EXEC 16384 16384 100 $THREADS $F1 $F2 /dev/null

# echo "numactl -N 0 -i 0,1 $EXEC 16384 16384 100 4 $F1 $F2 /dev/null"
# numactl -N 0 -i 0,1 $EXEC 16384 16384 100 4 $F1 $F2 /dev/null

# echo "numactl -N 0 -m 1 $EXEC 16384 16384 100 4 $F1 $F2 /dev/null"
# numactl -N 0 -m 1 $EXEC 16384 16384 100 4 $F1 $F2 /dev/null

# echo "numactl -N 0 -i 0,2 $EXEC 16384 16384 100 $THREADS $F1 $F2 /dev/null"
# numactl -N 0 -i 0,2 $EXEC 16384 16384 100 $THREADS $F1 $F2 /dev/null

echo "numactl -N 0 -m 2 $EXEC 16384 16384 100 $THREADS $F1 $F2 /dev/null"
numactl -N 0 -m 2 $EXEC 16384 16384 100 $THREADS $F1 $F2 /dev/null

