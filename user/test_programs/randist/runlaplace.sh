#!/bin/sh

LD_LIBRARY_PATH=/usr/local/lib/
export LD_LIBRARY_PATH
numactl -m 0 ./laplace
