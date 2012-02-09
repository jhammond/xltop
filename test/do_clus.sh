#!/bin/bash

clus="ranger"
xid=$(printf "%llx" $RANDOM)
now=$(date +%s)
sig=11

(
    echo "%clus_connect ${xid} ${clus} root ${now} 11"
    ./qhost < qhost-j.0
    # sleep 60
    # ./qhost < qhost-j.1
    # sleep 60
) | nc localhost 9901
