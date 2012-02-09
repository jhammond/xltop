#!/bin/bash

./do_mds.sh 1 &
./do_mds.sh 3 &
./do_mds.sh 5 &

for ((i = 1; i <= 20; i++)); do (./do_oss.sh $i &) ; done
for ((i = 23; i <= 72; i++)); do (./do_oss.sh $i &) ; done

./do_clus.sh
