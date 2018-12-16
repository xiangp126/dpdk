VIP=2001::221
RS=(
# "2001::173"
# "2001::174"
"2001::175"
)
LIP=(
"2001::220"
# "2001::221"
# "2001::222"
# "2001::223"
# "2001::224"
# "2001::225"
# "2001::226"
# "2001::227"
# "2001::228"
# "2001::229"
# "2001::230"
)
./dpip addr add $VIP/64 dev dpdk0
# ./dpip route add default via 10.123.31.254 dev dpdk0
./ipvsadm -A -u [$VIP]:80 -s rr
for rs in ${RS[@]}; do
    ./ipvsadm -a -u [$VIP]:80 -r [$rs]:6000 -b
done
for lip in ${LIP[@]}; do
    ./ipvsadm --add-laddr -z $lip -u [$VIP]:80 -F dpdk0
done
