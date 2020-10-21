#!/bin/bash

#Constants
AGE_THRESHOLD=0
EMA=1
HAMMING_WEIGHT=2

#Runtime Parameters
TARGET_PATH=$1
LAUNCHER='./bin/launcher'
RATIO=4
POLICY=$EMA
ALPHA=256 # Out of 1024!
THETA=1000
SCAN_SECONDS=(1 2 5)
SCAN_NSECONDS=(0 0 0) 
WARMUP_SCANS=(10 5 2)
SCANS_PER_MIG=(1 1 1)
MAX_MIGRATIONS=(40000 40000 40000)
TO_RUN=$2
MIGRATIONS="-m"

OUTDIR=$3

for i in ${!SCAN_SECONDS[@]}; do
    FILE="${OUTDIR}/data-${RATIO}-${ALPHA}-${SCAN_SECONDS[i]}-${SCAN_NSECONDS[i]}-${SCANS_PER_MIG[i]}-${MAX_MIGRATIONS[i]}.txt"
    echo $FILE
    sudo dmesg --clear
    $LAUNCHER --target-path $TARGET_PATH \
	      --ratio $RATIO \
	      --policy $POLICY \
 	      --alpha $ALPHA \
 	      --theta $THETA \
	      --scan-period-sec ${SCAN_SECONDS[i]} \
	      --scan-period-nsec ${SCAN_NSECONDS[i]} \
              --warmup-scans ${WARMUP_SCANS[i]} \
              --migration-cycle ${SCANS_PER_MIG[i]} \
              --max-migrations ${MAX_MIGRATIONS[i]} \
	      $MIGRATIONS \
	      $TO_RUN > $FILE
    sudo dmesg >> $FILE
done

# SCAN_SECONDS=(0 0 0 0 1
#               1 1 1 1 2
#               2 2 2 2 3
#               3 3 3 3 4
#               4 4 4 4 5
#              )
# SCAN_NSECONDS=(200000000 400000000 600000000 800000000 0
#                200000000 400000000 600000000 800000000 0
#                200000000 400000000 600000000 800000000 0
#                200000000 400000000 600000000 800000000 0
#                200000000 400000000 600000000 800000000 0
#               ) 
# WARMUP_SCANS=(50 25 16 12 10
#               5 5 5 5 5
#               2 2 2 2 2
#               1 1 1 1 1
#               1 1 1 1 1
#              )
# SCANS_PER_MIG=(1 1 1 1 1)
# MAX_MIGRATIONS=(8000 16000 24000 32000 40000
#                 40000 40000 40000 40000 40000
#                 40000 40000 40000 40000 40000
#                 40000 40000 40000 40000 40000
#                 40000 40000 40000 40000 40000
#                )
