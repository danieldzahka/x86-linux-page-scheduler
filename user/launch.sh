#!/bin/bash

#Constants
AGE_THRESHOLD=0
EMA=1
HAMMING_WEIGHT=2

#Runtime Parameters
TARGET_PATH="/home/dzahka3/rodinia_3.1/openmp/lud/omp/lud_omp"
LAUNCHER='/home/dzahka3/x86-linux-page-scheduler/user/bin/launcher'
RATIO=$1
POLICY=$EMA
ALPHA=64
THETA=3000
SCAN_SECONDS=1
SCAN_NSECONDS=0
TO_RUN=$2
MIGRATIONS="-m"

$LAUNCHER --target-path $TARGET_PATH \
	  --ratio $RATIO \
	  --policy $POLICY \
 	  --alpha $ALPHA \
 	  --theta $THETA \
	  --scan-period-sec $SCAN_SECONDS \
	  --scan-period-nsec $SCAN_NSECONDS \
	  $MIGRATIONS \
	  $TO_RUN 
