#!/bin/bash

rm -rf certs/*
make debug
exit
ret=1
while [[ $ret != 0 ]]; do
    ./app 8443
    ret=$?
    echo "Return code is $ret"
done
