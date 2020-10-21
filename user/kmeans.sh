#!/bin/bash

# echo 3 | sudo tee /proc/sys/vm/drop_caches

EXEC=$(readlink -f ../rodinia_3.1/openmp/kmeans/kmeans_openmp/kmeans)
FILE="../rodinia_3.1/openmp/kmeans/10000000_34f.txt"

# echo numactl -N 0 -m 0 $EXEC -n 16 -i $FILE
# numactl -N 0 -m 0 $EXEC -n 16 -i $FILE

# echo numactl -N 0 -i 0,1 $EXEC -n 16 -i $FILE
# numactl -N 0 -i 0,1 $EXEC -n 16 -i $FILE

# echo numactl -N 0 -m 1 $EXEC -n 16 -i $FILE
# numactl -N 0 -m 1 $EXEC -n 16 -i $FILE

# echo numactl -N 0 -i 0,2 $EXEC -n 16 -i $FILE
# numactl -N 0 -i 0,2 $EXEC -n 16 -i $FILE

echo numactl -N 0 -m 2 $EXEC -n 16 -i $FILE
numactl -N 0 -m 2 $EXEC -n 16 -i $FILE

