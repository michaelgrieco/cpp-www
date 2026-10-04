#!/bin/bash

rm -rf certs/*
make debug
ret=1
while [[ $ret != 0 ]]; do
    ./app 8443
    ret=$?
    echo "Return code is $ret"
done
