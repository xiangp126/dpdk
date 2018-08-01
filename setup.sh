#!/bin/bash
make config T=x86_64-native-linuxapp-gcc
sed -ri 's,(PMD_PCAP=).*,\1y,' build/.config

make -j 40

if [[ -f /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages ]]; then
    echo 5120 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
    echo 5120 > /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages
fi

cat << _EOF
-------------------------------------------------------- Print -------------------
RTE_SDK=`pwd`

echo 5120 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
echo 5120 > /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages

echo 0 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
echo 0 > /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages

-- or

echo 10 > /sys/devices/system/node/node0/hugepages/hugepages-1048576kB/nr_hugepages
echo 10 > /sys/devices/system/node/node1/hugepages/hugepages-1048576kB/nr_hugepages

echo 0 > /sys/devices/system/node/node0/hugepages/hugepages-1048576kB/nr_hugepages
echo 0 > /sys/devices/system/node/node1/hugepages/hugepages-1048576kB/nr_hugepages

mkdir -p /mnt/huge
mount -t hugetlbfs nodev /mnt/huge

export EXTRA_CFLAGS="-O0 -g3"
-------------------------------------------------------- -------------------------
_EOF
