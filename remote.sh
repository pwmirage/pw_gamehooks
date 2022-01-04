#!/bin/bash

IP=$1
shift
cmd="$@"

if [[ -z "$cmd" ]]; then
    cmd="hook"
fi

ret=0
if [[ $cmd == "hook" ]]; then
    ret=1
    makeout=$(make -n)
    if [[ $makeout == *"make: Nothing to be done"* ]]; then
        echo $makeout
        ret=0
    else
        while IFS="\n" read tmpcmd
        do
            echo "$" $tmpcmd
            out=$(nc -n $IP 61171 <<< "${tmpcmd//\"\"/\"\\\"}")
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
fi

if [[ $ret == "0" ]]; then
    nc -n $IP 61171 <<< "$cmd"
fi
