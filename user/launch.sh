#!/bin/bash

#Constants
AGE_THRESHOLD=0
EMA=1
HAMMING_WEIGHT=2

#Runtime Parameters
TARGET_PATH="/home/dzahka3/rodinia_3.1/openmp/hotspot/hotspot"
LAUNCHER='/home/dzahka3/x86-linux-page-scheduler/user/bin/launcher'
RATIO=$1
POLICY=$EMA
ALPHA=1024 # Out of 1024!
THETA=800
SCAN_SECONDS=1
SCAN_NSECONDS=0
WARMUP_SCANS=5
SCANS_PER_MIG=1
MAX_MIGRATIONS=20000 #Cap on migrations per period
TO_RUN=$2
MIGRATIONS=""

$LAUNCHER --target-path $TARGET_PATH \
	  --ratio $RATIO \
	  --policy $POLICY \
 	  --alpha $ALPHA \
 	  --theta $THETA \
	  --scan-period-sec $SCAN_SECONDS \
	  --scan-period-nsec $SCAN_NSECONDS \
          --warmup-scans $WARMUP_SCANS \
          --migration-cycle $SCANS_PER_MIG \
          --max-migrations $MAX_MIGRATIONS \
	  $MIGRATIONS \
	  $TO_RUN 
