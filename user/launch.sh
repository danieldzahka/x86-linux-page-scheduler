#!/bin/bash

#Constants
AGE_THRESHOLD=0
EMA=1
HAMMING_WEIGHT=2

#Runtime Parameters
TARGET_PATH='/home/dzahka3/Galois/build_dir/lonestar/pagerank/pagerank-push'
LAUNCHER='/home/dzahka3/x86-linux-page-scheduler/user/bin/launcher'
POLICY=$EMA
ALPHA=64
THETA=3000
SCAN_SECONDS=1
SCAN_NSECONDS=0
TO_RUN=$1
MIGRATIONS=""

$LAUNCHER --target-path $TARGET_PATH \
	  --policy $POLICY \
 	  --alpha $ALPHA \
 	  --theta $THETA \
	  --scan-period-sec $SCAN_SECONDS \
	  --scan-period-nsec $SCAN_NSECONDS \
	  $MIGRATIONS \
	  $TO_RUN 
