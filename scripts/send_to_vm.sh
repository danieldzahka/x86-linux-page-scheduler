#!/bin/bash

MODULE_OBJ='/home/dzahka3/guest-kernel/AOSProject/mod/pg_sched.ko'
INSTALL_SCRIPT='/home/dzahka3/guest-kernel/AOSProject/mod/install.sh'
DEST='daniel@192.168.122.252:~/'
scp $MODULE_OBJ $DEST
scp $INSTALL_SCRIPT $DEST

