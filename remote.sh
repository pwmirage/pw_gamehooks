#!/bin/bash

IP=$1
echo "hook" | nc -n $IP 61171
