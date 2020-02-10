#!/bin/bash

TARGET_PATH='/bin/ls'
LAUNCHER='/home/dzahka3/x86-linux-page-scheduler/user/bin/launcher'
PERIODS_TO_BE_COLD=10
SCAN_SECONDS=1
SCAN_NSECONDS=0
TO_RUN=$1

$LAUNCHER --target-path $TARGET_PATH \
 	  --periods-to-be-cold $PERIODS_TO_BE_COLD \
	  --scan-period-sec $SCAN_SECONDS \
	  --scan-period-nsec $SCAN_NSECONDS \
	  $TO_RUN 
