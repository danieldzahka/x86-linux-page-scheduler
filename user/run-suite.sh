#!/bin/bash

LAUNCH_BASE="./launch-suite-basineles.sh"
OUTDIR="/home/dzahka3/new-baselines/"
APPS=(
    backprop.sh
    hotspot.sh
    kmeans.sh
    # lud.sh
)
APP_PATHS=(
           /home/dzahka3/rodinia_3.1/openmp/backprop/backprop
           /home/dzahka3/rodinia_3.1/openmp/hotspot/hotspot
           /home/dzahka3/rodinia_3.1/openmp/kmeans/kmeans_openmp/kmeans
           # /home/dzahka3/rodinia_3.1/openmp/lud/omp/lud_omp
          )

# mkdir -p $OUTDIR
# # # Perform Baselines without migrations
# for i in ${!APP_PATHS[@]}; do
#     BASE=$(echo ${APPS[$i]} | perl -nle 'm/(.+).sh/ and print $1;')
#     $LAUNCH_BASE ${APP_PATHS[$i]} ${APPS[$i]} $OUTDIR "${BASE}.txt"
# done

LAUNCH='./launch2.sh'
RESULTS_DIR='/home/dzahka3/results-20-80-ema-more/'
for i in ${!APP_PATHS[@]}; do
    BASE=$(echo ${APPS[$i]} | perl -nle 'm/(.+).sh/ and print $1;')
    mkdir -p "${RESULTS_DIR}${BASE}"
    $LAUNCH ${APP_PATHS[$i]} ${APPS[$i]} "${RESULTS_DIR}${BASE}" 
done

