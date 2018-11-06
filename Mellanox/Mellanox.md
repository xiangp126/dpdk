## Mellanox

### Contents
- [System Info](#system)
- [Bifurcated Driver](#intro)
- [Remove ununsed NIC](#remove)
- [Install Mellanox Driver](#driver)
- [Compile DPVS](#dpvs)
- [Launch DPVS](#launch)

<a id=system></a>
### System Info

```bash
uname -r
3.10.0-693.21.1.el7.x86_64

cat /etc/redhat-release
CentOS Linux release 7.4.1708 (Core)
```
网卡型号 `MT27710 Family [ConnectX-4 Lx] 1015`

<a id=intro></a>
### Intro
Mellanox NIC differs with Intel, it uses `Bifurcated Driver`
<https://doc.dpdk.org/guides/linux_gsg/linux_drivers.html#bifurcated-driver>

<a id=remove></a>
### Remove Unused NIC
if you have 2 Mellanox NIC in you system ,and you plan to use only one of them, please remove the one not to use from Linux first

    0000:5e:00.0 'MT27710 Family [ConnectX-4 Lx] 1015' if=enp94s0f0 drv=mlx5_core unused=
    0000:5e:00.1 'MT27710 Family [ConnectX-4 Lx] 1015' if=enp94s0f1 drv=mlx5_core unused= *Active*

say the one will not use is 

    0000:5e:00.0 'MT27710 Family [ConnectX-4 Lx] 1015' if=enp94s0f0 drv=mlx5_core unused= *Active*

- shutdown the card

```bash
# ifdown enp94s0f0
ifconfig enp94s0f0 down
./usertools/dpdk-devbind.py -u 0000:5e:00.0
```

- check the status

```bash
./usertools/dpdk-devbind.py --status

Network devices using DPDK-compatible driver
============================================
<none>

Network devices using kernel driver
===================================
0000:04:00.0 'NetXtreme BCM5720 Gigabit Ethernet PCIe 165f' if=eth0 drv=tg3 unused= *Active*
0000:04:00.1 'NetXtreme BCM5720 Gigabit Ethernet PCIe 165f' if=eth1 drv=tg3 unused=
0000:5e:00.1 'MT27710 Family [ConnectX-4 Lx] 1015' if=enp94s0f1 drv=mlx5_core unused= *Active*

Other Network devices
=====================
0000:5e:00.0 'MT27710 Family [ConnectX-4 Lx] 1015' unused=mlx5_core

```

<a id=driver></a>
### Install Mellanox Driver
- download and install

> Download from <http://www.mellanox.com/page/products_dyn?product_family=26><br>
> Install *MLNX\_OFED\_LINUX-4.4-1.0.0.0-rhel7.4-x86_64.tgz*<br>
> refer <http://doc.dpdk.org/guides/nics/mlx5.html?highlight=mlx5#quick-start-guide-on-ofed>

```bash
./mlnxofedinstall --upstream-libs --dpdk

......

[MLNX_OFED_LINUX-4.4-1.0.0.0-rhel7.4-x86_64]# ibv_devinfo
ibv_devinfo
hca_id: mlx5_1
        transport:                      InfiniBand (0)
        fw_ver:                         14.23.1000
        node_guid:                      ec0d:9a03:0071:7041
        sys_image_guid:                 ec0d:9a03:0071:7040
        vendor_id:                      0x02c9
        vendor_part_id:                 4117
        hw_ver:                         0x0
        board_id:                       MT_2470112034
        phys_port_cnt:                  1
                port:   1
                        state:                  PORT_ACTIVE (4)
                        max_mtu:                4096 (5)
                        active_mtu:             1024 (3)
                        sm_lid:                 0
                        port_lid:               0
                        port_lmc:               0x00
                        link_layer:             Ethernet
```
- modprobe & restart service

```bash
modprobe -a ib_uverbs mlx5_core mlx5_ib
/etc/init.d/openibd restart
```

<a id=dpdk></a>
### Compile DPDK
> refer <http://doc.dpdk.org/guides/nics/mlx5.html?highlight=mlx5><br>
> DPDK version： `17.11.2` LTS
   

```bash
make config T=x86_64-native-linuxapp-gcc
sed -ri 's,(PMD_PCAP=).*,\1y,' build/.config

vim config/common_base
# setting CONFIG_RTE_LIBRTE_MLX5_PMD=y

make -j

echo 5120 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
echo 5120 > /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages
```

> Mellanox NIC **need not use./usertools/dpdk-devbind.py** to bind card<br>
> Do not use igb_uio/uio etc.

~~insmod build/kmod/igb_uio.ko~~<br>
~~insmod build/kmod/rte_kni.ko~~

<a id=dpvs></a>
### Compile DPVS
> add `-libverbs -lmlx5 -lrte_pmd_mlx5` into Makefile

```git
diff --git a/src/dpdk.mk b/src/dpdk.mk
index a7f078c..aff002c 100644
--- a/src/dpdk.mk
+++ b/src/dpdk.mk
@@ -39,7 +39,8 @@ CFLAGS += -march=native \
 LIBS += -L $(DPDKDIR)/lib

 LIBS += -Wl,--no-as-needed -fvisibility=default \
-        -Wl,--whole-archive -lrte_pmd_vmxnet3_uio -lrte_pmd_i40e -lrte_pmd_ixgbe \
+               -Wl,--whole-archive -lrte_pmd_vmxnet3_uio -lrte_pmd_i40e -lrte_pmd_ixgbe \
+               -lrte_pmd_mlx5 -libverbs -lmlx5 \
                -lrte_pmd_e1000 -lrte_pmd_bnxt -lrte_pmd_ring -lrte_pmd_bond -lrte_ethdev -lrte_ip_frag \
                -Wl,--whole-archive -lrte_hash -lrte_kvargs -Wl,-lrte_mbuf -lrte_eal \
                -Wl,-lrte_mempool -lrte_ring -lrte_cmdline -lrte_cfgfile -lrte_kni \
```

```bash
export RTE_SDK=<Path-to-DPDK-Root>
# export RTE_TARGET=build
make -j
```

<a id=launch></a>
### Launch DPVS

```bash
./src/dpvs
```

If occurs No DPDK ports found

```
EAL: Error - exiting with code: 1
  Cause: No dpdk ports found!
Possibly nic or driver is not dpdk-compatible.
```

you should

1. double check link library of `-lrte_pmd_mlx5 -libverbs -lmlx5` for DPVS
2. `ldconfig` again