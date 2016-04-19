#!/bin/bash

URI_FILE=$1
LINES=$2
ARRIVALS=( 10 100 200 300 400 )
REMOVALS=( 10 100 200 300 400 )

INDEX=0
MAX_INDEX=5
while [ $INDEX -lt $MAX_INDEX ]
do 
    AR=${ARRIVALS[INDEX]}
    RR=${REMOVALS[INDEX]}
        
    FILE=${URI_FILE##*/}
    OUT=${FILE%.*}_${AR}_${RR}.csv
    echo ${OUT}
    ./pit_test ${URI_FILE} ${LINES} ${AR} ${RR} > $OUT

    INDEX=$[$INDEX+1]
done