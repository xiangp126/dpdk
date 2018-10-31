#!/bin/bash
# setup DPVS ip info
# mark vip here
VIP=
# mark rs here, one ip per line
RS=(
"192.168.10.10"
"192.168.10.1x"
)
LIP=(
"192.168.10.170"
"192.168.10.17x"
)
./dpip addr add $VIP/24 dev dpdk0
# ./dpip route add default via 10.xx.xx.xx dev dpdk0
./ipvsadm -A -t $VIP:80 -s rr
for rs in ${RS[@]}; do
    ./ipvsadm -a -t $VIP:80 -r $rs -b
done
for lip in ${LIP[@]}; do
    ./ipvsadm --add-laddr -z $lip -t $VIP:80 -F dpdk0
done
