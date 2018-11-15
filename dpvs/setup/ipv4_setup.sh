VIP=192.168.10.1
# 10.2.10.174 192.168.10.11
RS=(
"192.168.10.10"
"192.168.10.11"
"192.168.10.12"
)
LIP=(
"192.168.10.170"
"192.168.10.171"
"192.168.10.172"
"192.168.10.173"
"192.168.10.174"
"192.168.10.176"
"192.168.10.177"
"192.168.10.178"
"192.168.10.179"
"192.168.10.180"
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
