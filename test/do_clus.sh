#!/bin/bash

addr=localhost:9901
clus="ranger.tacc.utexas.edu"

./qhost < test/qhost-j.0 | curl -v --data-binary @- -XPUT http://${addr}/clus/${clus}
