#!/bin/bash

addr=localhost:9901
nid_file=test/client-nids
prog=$(basename $0)

if [ $# -ne 1 ]; then
    echo "Usage: ${prog} N" >&2
    exit 1
fi

serv="oss$1.ranger.tacc.utexas.edu"

v_arg=""
if [ -n "$V" ]; then
    v_arg="-v"
fi

while true; do
    (
        shuf $nid_file | sed 1024q | while read nid; do
            echo $nid $((RANDOM * 1024)) $((RANDOM * 1024)) $((RANDOM / 1024))
        done
    ) | curl $v_arg --data-binary @- -XPUT http://${addr}/serv/${serv}
    sleep 30
done
