#!/bin/bash

if [ -c /dev/pg_sched ]; then
    sudo rmmod pg_sched
fi

sudo insmod pg_sched.ko pg_sched_debug=1
sudo chmod 666 /dev/pg_sched
