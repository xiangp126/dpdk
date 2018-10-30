#!/bin/bash
for (( i = 1; i <= 16; i++ )); do
    # prompt if file already exist
    # cp -i ../dpvs.conf dpvs_$i.conf

    # open each file consecutively
    vim dpvs_$i.conf
done
