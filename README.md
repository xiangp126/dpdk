## DPDK
### Illustrate
DPDK is a set of libraries and drivers for fast packet processing.
It supports many processor architectures and both FreeBSD and Linux.

The DPDK uses the Open Source BSD license for the core libraries and
drivers. The kernel components are GPLv2 licensed.

Please check the doc directory for release notes,
API documentation, and sample application information.

For questions and usage discussions, subscribe to: users@dpdk.org
Report bugs and issues to the development mailing list: dev@dpdk.org

### Prerequisite

#### CentOS

```bash
yum update -y
yum install numactl-devel libpcap-devel popt-devel kernel-devel -y
```

#### Ubuntu

```bash
apt-get install libnuma-dev libpcap-dev -y
```

### Quick start

<http://www.dpdk.org/doc/quick-start>

```bash
make config T=x86_64-native-linuxapp-gcc
sed -ri 's,(PMD_PCAP=).*,\1y,' build/.config

make -j 40

mkdir -p /mnt/huge
mount -t hugetlbfs nodev /mnt/huge
```

### Build
- export var

```bash
# cd 'dpdk main directory'
export RTE_SDK=`pwd`
# export RTE_TARGET=build
```

- build examples

```bash
# cd 'dpdk main directory'
cd example
make -j
```

- build `dpvs`

```bash
# cd 'dpvs main directory'
make -j
```

### Debug

_for `dpvs` **step into** `dpdk` functions_

```bash
# cd 'dpdk main directory'
export EXTRA_CFLAGS="-O0 -g3"
make -j
```

_jump into `dpvs` main directory_

```bash
make clean
make -j
```

_then relaunch `cgdb`_

```bash
cgdb src/dpvs
```
