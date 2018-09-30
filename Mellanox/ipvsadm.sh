#!/bin/bash
VIP=
RS=(
# one ip per line
)
LIP=(
# one ip per line
)
./dpip addr add $VIP/24 dev dpdk0
# ./dpip route add default via 10.123.31.254 dev dpdk0
./ipvsadm -A -t $VIP:80 -s rr
for rs in ${RS[@]}; do
    ./ipvsadm -a -t $VIP:80 -r $rs -b
done
for lip in ${LIP[@]}; do
    ./ipvsadm --add-laddr -z $lip -t $VIP:80 -F dpdk0
done
