#!/bin/bash

serv="oss$1.ranger.tacc.utexas.edu"
xid=$(printf "%llx" $RANDOM)
user=root
now=$(date +%s)
auth=11

n0=$(( 96 * 256 + 1 ))
n1=$(( 115 * 256 + 96 + 1))
(
    echo "%serv_connect ${xid} ${serv} ${user} ${now} ${auth}"
    while true; do
        ir=$((RANDOM * (n1 - n0) / 32768 + n0))
        i0=$((ir % 256))
        i1=$((ir / 256))
        echo "129.114.${i1}.${i0}@o2ib $RANDOM $RANDOM $RANDOM"
        sleep 0.1
    done
) | nc -q 20 localhost 9901
