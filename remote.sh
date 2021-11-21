#!/bin/bash

IP=$1
makeout=$(make -n)

ret=1
if [[ $makeout == *"make: Nothing to be done"* ]]; then
    echo $makeout
    ret=0
else
    while IFS="\n" read cmd
    do
        echo "$" $cmd
        out=$(nc -n $IP 61171 <<< "${cmd//\"\"/\"\\\"}")
        if [[ $out == "" ]]; then
            ret=0
        else
            IFS=
            echo $out
            ret=1
            break
        fi
    done <<< "$makeout"
fi

if [[ $ret == "0" ]]; then
    nc -n $IP 61171 <<< "hook"
fi
