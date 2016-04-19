#!/bin/bash

URI_FILE=$1
LINES=$2
ARRIVALS=( 10 100 200 300 400 )
REMOVALS=( 10 100 200 300 400 )

for AR in "${ARRIVALS[@]}"
do
    for RR in "#{REMOVALS[@]}"
    do
        OUT=${URI_FILE}_${AR}_${RR}.csv
        ./pit_test ${URI_FILE} ${LINES} ${AR} ${RR} > $OUT
    done 
done