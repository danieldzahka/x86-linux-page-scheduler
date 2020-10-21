#!/bin/bash

# echo 3 | sudo tee /proc/sys/vm/drop_caches

# echo numactl -N 0 -m 0 /home/dzahka3/rodinia_3.1/openmp/kmeans/kmeans_openmp/kmeans -n 16 -i /home/dzahka3/rodinia_3.1/openmp/kmeans/10000000_34f.txt
# numactl -N 0 -m 0 /home/dzahka3/rodinia_3.1/openmp/kmeans/kmeans_openmp/kmeans -n 16 -i /home/dzahka3/rodinia_3.1/openmp/kmeans/10000000_34f.txt

# echo numactl -N 0 -i 0,1 /home/dzahka3/rodinia_3.1/openmp/kmeans/kmeans_openmp/kmeans -n 16 -i /home/dzahka3/rodinia_3.1/openmp/kmeans/10000000_34f.txt
# numactl -N 0 -i 0,1 /home/dzahka3/rodinia_3.1/openmp/kmeans/kmeans_openmp/kmeans -n 16 -i /home/dzahka3/rodinia_3.1/openmp/kmeans/10000000_34f.txt

# echo numactl -N 0 -m 1 /home/dzahka3/rodinia_3.1/openmp/kmeans/kmeans_openmp/kmeans -n 16 -i /home/dzahka3/rodinia_3.1/openmp/kmeans/10000000_34f.txt
# numactl -N 0 -m 1 /home/dzahka3/rodinia_3.1/openmp/kmeans/kmeans_openmp/kmeans -n 16 -i /home/dzahka3/rodinia_3.1/openmp/kmeans/10000000_34f.txt

# echo numactl -N 0 -i 0,2 /home/dzahka3/rodinia_3.1/openmp/kmeans/kmeans_openmp/kmeans -n 16 -i /home/dzahka3/rodinia_3.1/openmp/kmeans/10000000_34f.txt
# numactl -N 0 -i 0,2 /home/dzahka3/rodinia_3.1/openmp/kmeans/kmeans_openmp/kmeans -n 16 -i /home/dzahka3/rodinia_3.1/openmp/kmeans/10000000_34f.txt

echo numactl -N 0 -m 2 /home/dzahka3/rodinia_3.1/openmp/kmeans/kmeans_openmp/kmeans -n 16 -i /home/dzahka3/rodinia_3.1/openmp/kmeans/10000000_34f.txt
numactl -N 0 -m 2 /home/dzahka3/rodinia_3.1/openmp/kmeans/kmeans_openmp/kmeans -n 16 -i /home/dzahka3/rodinia_3.1/openmp/kmeans/10000000_34f.txt

