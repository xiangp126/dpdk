## Mellanox

### Contents
- [Demo System Info](#system)
- [Introduction for Bifurcated Driver](#bifurcated)
- [Install Mellanox Driver](#driver)
- [Adjust Card to be Used](#remove)
- [Build DPDK](#dpdk)
- [Build DPVS](#dpvs)
- [hard Problems Fix](#debug)
- [Mellanox Firmware Tools (MFT)](#mft)

<a id=system></a>
### Demo System Info
```bash
uname -r
3.10.0-693.21.1.el7.x86_64

cat /etc/redhat-release
CentOS Linux release 7.4.1708 (Core)
```
网卡型号 `MT27710 Family [ConnectX-4 Lx] 1015`

<a id=bifurcated></a>
### Bifurcated Driver
`Mellanox` NIC differs with `Intel`, it uses `Bifurcated Driver`
<https://doc.dpdk.org/guides/linux_gsg/linux_drivers.html#bifurcated-driver>

<a id=remove></a>
### Adjust Card to be Used
_Normally after installing `Mellanox Driver`, all Mellanox cards that was available will be `Active`_

> If you have **1 more** Mellanox card in you system ,but you plan to use **only 1** of them<br>
you need to **remove** that from `Linux` first

#### get card info
```bash
# ./usertools/dpdk-devbind.py --status

Network devices using DPDK-compatible driver
============================================
<none>

Network devices using kernel driver
===================================
0000:04:00.0 'NetXtreme BCM5720 Gigabit Ethernet PCIe 165f' if=eth0 drv=tg3 unused= *Active*
0000:04:00.1 'NetXtreme BCM5720 Gigabit Ethernet PCIe 165f' if=eth1 drv=tg3 unused=
0000:5e:00.0 'MT27710 Family [ConnectX-4 Lx] 1015' if=enp94s0f0 drv=mlx5_core unused= *Active*
0000:5e:00.1 'MT27710 Family [ConnectX-4 Lx] 1015' if=enp94s0f1 drv=mlx5_core unused= *Active*
```

_say the one to be removed is_

    0000:5e:00.0 'MT27710 Family [ConnectX-4 Lx] 1015' if=enp94s0f0 drv=mlx5_core unused= *Active*

#### shutdown the card
```bash
# ifdown enp94s0f0
ifconfig enp94s0f0 down

```

#### remove the card
```bash
./usertools/dpdk-devbind.py -u 0000:5e:00.0
```

#### check the result
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
### Mellanox Driver
#### download
> Download from <http://www.mellanox.com/page/products_dyn?product_family=26>

Install `MLNX_OFED_LINUX-4.4-1.0.0.0-rhel7.4-x86_64.tgz` according to your `OS` type

#### install
> refer <http://doc.dpdk.org/guides/nics/mlx5.html?highlight=mlx5#quick-start-guide-on-ofed>

```bash
cd ~
tar -xv -f MLNX_OFED_LINUX-4.4-1.0.0.0-rhel7.4-x86_64.tgz
cd MLNX_OFED_LINUX-4.4-1.0.0.0-rhel7.4-x86_64
./mlnxofedinstall --upstream-libs --dpdk
```

#### check device status
> make sure device **`status`** was **`PORT_ACTIVE (4)`**. if not, refer [Hrad Problem](#debug)

```bash
# ibv_devinfo
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

#### modprobe
```
# modprobe -a ib_uverbs mlx5_core mlx5_ib
```

#### ~~restart service[Not Need]~~
_only needed for restart service_

```
# /etc/init.d/openibd restart
```

<a id=dpdk></a>
### Build DPDK
> refer <http://doc.dpdk.org/guides/nics/mlx5.html?highlight=mlx5><br>
DPDK version：`17.11.2` LTS

#### modify config

_make `dpdk` to build **librte\_mlx5\_pmd**_

~~must do this before `make config`~~

```git
# git diff
diff --git a/config/common_base b/config/common_base
index 214d9a2..1adf9d6 100644
--- a/config/common_base
+++ b/config/common_base
@@ -235,7 +235,7 @@ CONFIG_RTE_LIBRTE_MLX4_TX_MP_CACHE=8
 #
 # Compile burst-oriented Mellanox ConnectX-4 & ConnectX-5 (MLX5) PMD
 #
-CONFIG_RTE_LIBRTE_MLX5_PMD=n
+CONFIG_RTE_LIBRTE_MLX5_PMD=y
 CONFIG_RTE_LIBRTE_MLX5_DEBUG=n
 CONFIG_RTE_LIBRTE_MLX5_TX_MP_CACHE=8

```

#### make config
```bash
make config T=x86_64-native-linuxapp-gcc
sed -ri 's,(PMD_PCAP=).*,\1y,' build/.config
```

#### compile dpdk
```bash
make -j
```

#### reserve hugepages
```bash
echo 5120 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
echo 5120 > /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages
```

#### insmod rte_kni

> Mellanox NIC **don't use./usertools/dpdk-devbind.py** to bind card<br>
Do not use `igb_uio.ko`, but `rte_kni.ko` will be used.

~~insmod build/kmod/igb_uio.ko~~<br>

```bash
insmod build/kmod/rte_kni.ko
```

<a id=dpvs></a>
### Build DPVS
#### modify makefile
> add `-libverbs -lmlx5 -lrte_pmd_mlx5` for dependency

```git
# git diff
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

#### environment variable
```bash
export RTE_SDK=<Path-to-DPDK-Root>
# export RTE_TARGET=build
```

#### compile dpvs
```bash
make -j
```

#### launch dpvs
```bash
./src/dpvs
```

<a id=debug></a>
### Hard Problem Fix
#### no DPDK ports found
> If launch dpvs followed with `No DPDK ports found`

```bash
EAL: Error - exiting with code: 1
  Cause: No dpdk ports found!
Possibly nic or driver is not dpdk-compatible.
```

- double check link library of `-lrte_pmd_mlx5 -libverbs -lmlx5` for dpvs
- `ldconfig` again

#### DPDK ports not match
`DPDK` ports found by `DPVS` not matched with that configured in `dpvs.conf`

refer [Adjust Card to be Used](#remove)

#### can not find `-lrte_pmd_mlx5`
you should do `modify config` before `make config` in [Build DPDK](#dpdk)

<a id=mft></a>
### Mellanox Firmware Tools (MFT)
download from <http://www.mellanox.com/page/management_tools>