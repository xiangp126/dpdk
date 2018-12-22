#!/bin/bash
WAN_IP=192.168.10.1       # WAN IP can access Internet.
# WAN_IP=10.123.30.31       # WAN IP can access Internet.
WAN_PREF=24               # WAN side network prefix length.
# GATEWAY=10.123.31.254     # WAN side gateway
GATEWAY=192.168.10.1        # WAN side gateway

LAN_IP=192.168.9.1
LAN_PREF=24

# add WAN-side IP with sapool
./dpip addr add $WAN_IP/$WAN_PREF dev dpdk0 sapool # must add sapool for WAN-side IP
# add LAN-side IP as well as LAN route (generated)
./dpip addr add $LAN_IP/$LAN_PREF dev dpdk0

# add default route for WAN interface
./dpip route add default via $GATEWAY dev dpdk0

# SNAT section
# -H MATCH       SNAT uses -H for "match" service instead of -t or -u
#                MATCH support "proto", "src-range", "oif" and "iif".
# -r <WIP:0>     used to specify the WAN IP after SNAT translation,
#                the "port" part must be 0.
# -J             for "SNAT" forwarding mode.
MATCH0='proto=tcp,rule-all=deny,src-range=192.168.9.10-192.168.9.254,oif=dpdk0'
# MATCH1='proto=icmp,rule-all=deny,src-range=192.168.9.2-192.168.9.254,oif=dpdk0'

./ipvsadm -A -s rr -H $MATCH0
./ipvsadm -a -H $MATCH0 -r $WAN_IP:0 -w 100 -J

# ./ipvsadm -A -s rr -H $MATCH1
# ./ipvsadm -a -H $MATCH1 -r $WAN_IP:0 -w 100 -J

ACL0='rule=permit,max-conn=5,src-range=192.168.9.5-192.168.9.10,dst-range=0.0.0.0-0.0.0.0:80-80'
ACL1='rule=permit,max-conn=0,src-range=192.168.9.15-192.168.9.18,dst-range=192.168.10.11:80'
# ACL2='rule=permit,max-conn=100,src-range=192.168.9.10-192.168.9.15'
./ipvsadm --add-acl --acl $ACL0 -H $MATCH0
./ipvsadm --add-acl --acl $ACL1 -H $MATCH0
# ./ipvsadm --add-acl --acl $ACL2 -H $MATCH1
