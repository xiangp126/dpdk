DPDK is a set of libraries and drivers for fast packet processing.
It supports many processor architectures and both FreeBSD and Linux.

The DPDK uses the Open Source BSD license for the core libraries and
drivers. The kernel components are GPLv2 licensed.

Please check the doc directory for release notes,
API documentation, and sample application information.

For questions and usage discussions, subscribe to: users@dpdk.org
Report bugs and issues to the development mailing list: dev@dpdk.org

## Quick start

```bash
http://www.dpdk.org/doc/quick-start

make config T=x86_64-native-linuxapp-gcc
sed -ri 's,(PMD_PCAP=).*,\1y,' build/.config

make -j 40

mkdir -p /mnt/huge
mount -t hugetlbfs nodev /mnt/huge
```

## Debug
```bash
export EXTRA_CFLAGS="-O0 -g3"

make -j
```
