#!/bin/bash

addr=localhost:9901
serv="oss$1.ranger.tacc.utexas.edu"

n0=$(( 96 * 256 + 1 ))
n1=$(( 115 * 256 + 96 + 1))

while true; do
    (
        for ((i = 0; i < 1024; i++)); do
            ir=$((RANDOM * (n1 - n0) / 32768 + n0))
            i0=$((ir % 256))
            i1=$((ir / 256))
            echo "129.114.${i1}.${i0}@o2ib $RANDOM $RANDOM $RANDOM"
        done
    ) | curl -v --data-binary @- -XPUT http://${addr}/serv/${serv}
    sleep 30
done
