#!/bin/bash

# Make dax0.0 appear as a numa node
sudo daxctl reconfigure-device --mode=system-ram dax0.0
