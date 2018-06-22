/*-
 *   BSD LICENSE
 *
 *   Copyright 2015 6WIND S.A.
 *   Copyright 2015 Mellanox.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of 6WIND S.A. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <unistd.h>

#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_interrupts.h>
#include <rte_alarm.h>

#include "mlx5.h"
#include "mlx5_rxtx.h"
#include "mlx5_utils.h"

/**
 * Stop traffic on Tx queues.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 */
static void
mlx5_txq_stop(struct rte_eth_dev *dev)
{
	struct priv *priv = dev->data->dev_private;
	unsigned int i;

	for (i = 0; i != priv->txqs_n; ++i)
		mlx5_txq_release(dev, i);
}

/**
 * Start traffic on Tx queues.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
static int
mlx5_txq_start(struct rte_eth_dev *dev)
{
	struct priv *priv = dev->data->dev_private;
	unsigned int i;
	int ret;

	/* Add memory regions to Tx queues. */
	for (i = 0; i != priv->txqs_n; ++i) {
		unsigned int idx = 0;
		struct mlx5_mr *mr;
		struct mlx5_txq_ctrl *txq_ctrl = mlx5_txq_get(dev, i);

		if (!txq_ctrl)
			continue;
		LIST_FOREACH(mr, &priv->mr, next) {
			mlx5_txq_mp2mr_reg(&txq_ctrl->txq, mr->mp, idx++);
			if (idx == MLX5_PMD_TX_MP_CACHE)
				break;
		}
		txq_alloc_elts(txq_ctrl);
		txq_ctrl->ibv = mlx5_txq_ibv_new(dev, i);
		if (!txq_ctrl->ibv) {
			rte_errno = ENOMEM;
			goto error;
		}
	}
	ret = mlx5_tx_uar_remap(dev, priv->ctx->cmd_fd);
	if (ret)
		goto error;
	return 0;
error:
	ret = rte_errno; /* Save rte_errno before cleanup. */
	mlx5_txq_stop(dev);
	rte_errno = ret; /* Restore rte_errno. */
	return -rte_errno;
}

/**
 * Stop traffic on Rx queues.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 */
static void
mlx5_rxq_stop(struct rte_eth_dev *dev)
{
	struct priv *priv = dev->data->dev_private;
	unsigned int i;

	for (i = 0; i != priv->rxqs_n; ++i)
		mlx5_rxq_release(dev, i);
}

/**
 * Start traffic on Rx queues.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
static int
mlx5_rxq_start(struct rte_eth_dev *dev)
{
	struct priv *priv = dev->data->dev_private;
	unsigned int i;
	int ret = 0;

	for (i = 0; i != priv->rxqs_n; ++i) {
		struct mlx5_rxq_ctrl *rxq_ctrl = mlx5_rxq_get(dev, i);

		if (!rxq_ctrl)
			continue;
		ret = rxq_alloc_elts(rxq_ctrl);
		if (ret)
			goto error;
		rxq_ctrl->ibv = mlx5_rxq_ibv_new(dev, i);
		if (!rxq_ctrl->ibv)
			goto error;
	}
	return 0;
error:
	ret = rte_errno; /* Save rte_errno before cleanup. */
	mlx5_rxq_stop(dev);
	rte_errno = ret; /* Restore rte_errno. */
	return -rte_errno;
}

/**
 * DPDK callback to start the device.
 *
 * Simulate device start by attaching all configured flows.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
int
mlx5_dev_start(struct rte_eth_dev *dev)
{
	struct priv *priv = dev->data->dev_private;
	struct mlx5_mr *mr = NULL;
	int ret;

	dev->data->dev_started = 1;
	ret = mlx5_flow_create_drop_queue(dev);
	if (ret) {
		DRV_LOG(ERR, "port %u drop queue allocation failed: %s",
			dev->data->port_id, strerror(rte_errno));
		goto error;
	}
	DRV_LOG(DEBUG, "port %u allocating and configuring hash Rx queues",
		dev->data->port_id);
	rte_mempool_walk(mlx5_mp2mr_iter, priv);
	ret = mlx5_txq_start(dev);
	if (ret) {
		DRV_LOG(ERR, "port %u Tx queue allocation failed: %s",
			dev->data->port_id, strerror(rte_errno));
		goto error;
	}
	ret = mlx5_rxq_start(dev);
	if (ret) {
		DRV_LOG(ERR, "port %u Rx queue allocation failed: %s",
			dev->data->port_id, strerror(rte_errno));
		goto error;
	}
	ret = mlx5_rx_intr_vec_enable(dev);
	if (ret) {
		DRV_LOG(ERR, "port %u Rx interrupt vector creation failed",
			dev->data->port_id);
		goto error;
	}
	mlx5_xstats_init(dev);
	ret = mlx5_traffic_enable(dev);
	if (ret) {
		DRV_LOG(DEBUG, "port %u failed to set defaults flows",
			dev->data->port_id);
		goto error;
	}
	ret = mlx5_flow_start(dev, &priv->flows);
	if (ret) {
		DRV_LOG(DEBUG, "port %u failed to set flows",
			dev->data->port_id);
		goto error;
	}
	dev->tx_pkt_burst = mlx5_select_tx_function(dev);
	dev->rx_pkt_burst = mlx5_select_rx_function(dev);
	mlx5_dev_interrupt_handler_install(dev);
	return 0;
error:
	ret = rte_errno; /* Save rte_errno before cleanup. */
	/* Rollback. */
	dev->data->dev_started = 0;
	for (mr = LIST_FIRST(&priv->mr); mr; mr = LIST_FIRST(&priv->mr))
		mlx5_mr_release(mr);
	mlx5_flow_stop(dev, &priv->flows);
	mlx5_traffic_disable(dev);
	mlx5_txq_stop(dev);
	mlx5_rxq_stop(dev);
	mlx5_flow_delete_drop_queue(dev);
	rte_errno = ret; /* Restore rte_errno. */
	return -rte_errno;
}

/**
 * DPDK callback to stop the device.
 *
 * Simulate device stop by detaching all configured flows.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 */
void
mlx5_dev_stop(struct rte_eth_dev *dev)
{
	struct priv *priv = dev->data->dev_private;
	struct mlx5_mr *mr;

	dev->data->dev_started = 0;
	/* Prevent crashes when queues are still in use. */
	dev->rx_pkt_burst = removed_rx_burst;
	dev->tx_pkt_burst = removed_tx_burst;
	rte_wmb();
	usleep(1000 * priv->rxqs_n);
	DRV_LOG(DEBUG, "port %u cleaning up and destroying hash Rx queues",
		dev->data->port_id);
	mlx5_flow_stop(dev, &priv->flows);
	mlx5_traffic_disable(dev);
	mlx5_rx_intr_vec_disable(dev);
	mlx5_dev_interrupt_handler_uninstall(dev);
	mlx5_txq_stop(dev);
	mlx5_rxq_stop(dev);
	for (mr = LIST_FIRST(&priv->mr); mr; mr = LIST_FIRST(&priv->mr))
		mlx5_mr_release(mr);
	mlx5_flow_delete_drop_queue(dev);
}

/**
 * Enable traffic flows configured by control plane
 *
 * @param dev
 *   Pointer to Ethernet device private data.
 * @param dev
 *   Pointer to Ethernet device structure.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
int
mlx5_traffic_enable(struct rte_eth_dev *dev)
{
	struct priv *priv = dev->data->dev_private;
	struct rte_flow_item_eth bcast = {
		.dst.addr_bytes = "\xff\xff\xff\xff\xff\xff",
	};
	struct rte_flow_item_eth ipv6_multi_spec = {
		.dst.addr_bytes = "\x33\x33\x00\x00\x00\x00",
	};
	struct rte_flow_item_eth ipv6_multi_mask = {
		.dst.addr_bytes = "\xff\xff\x00\x00\x00\x00",
	};
	struct rte_flow_item_eth unicast = {
		.src.addr_bytes = "\x00\x00\x00\x00\x00\x00",
	};
	struct rte_flow_item_eth unicast_mask = {
		.dst.addr_bytes = "\xff\xff\xff\xff\xff\xff",
	};
	const unsigned int vlan_filter_n = priv->vlan_filter_n;
	const struct ether_addr cmp = {
		.addr_bytes = "\x00\x00\x00\x00\x00\x00",
	};
	unsigned int i;
	unsigned int j;
	int ret;

	if (priv->isolated)
		return 0;
	if (dev->data->promiscuous) {
		struct rte_flow_item_eth promisc = {
			.dst.addr_bytes = "\x00\x00\x00\x00\x00\x00",
			.src.addr_bytes = "\x00\x00\x00\x00\x00\x00",
			.type = 0,
		};

		ret = mlx5_ctrl_flow(dev, &promisc, &promisc);
		if (ret)
			goto error;
	}
	if (dev->data->all_multicast) {
		struct rte_flow_item_eth multicast = {
			.dst.addr_bytes = "\x01\x00\x00\x00\x00\x00",
			.src.addr_bytes = "\x00\x00\x00\x00\x00\x00",
			.type = 0,
		};

		ret = mlx5_ctrl_flow(dev, &multicast, &multicast);
		if (ret)
			goto error;
	} else {
		/* Add broadcast/multicast flows. */
		for (i = 0; i != vlan_filter_n; ++i) {
			uint16_t vlan = priv->vlan_filter[i];

			struct rte_flow_item_vlan vlan_spec = {
				.tci = rte_cpu_to_be_16(vlan),
			};
			struct rte_flow_item_vlan vlan_mask = {
				.tci = 0xffff,
			};

			ret = mlx5_ctrl_flow_vlan(dev, &bcast, &bcast,
						  &vlan_spec, &vlan_mask);
			if (ret)
				goto error;
			ret = mlx5_ctrl_flow_vlan(dev, &ipv6_multi_spec,
						  &ipv6_multi_mask,
						  &vlan_spec, &vlan_mask);
			if (ret)
				goto error;
		}
		if (!vlan_filter_n) {
			ret = mlx5_ctrl_flow(dev, &bcast, &bcast);
			if (ret)
				goto error;
			ret = mlx5_ctrl_flow(dev, &ipv6_multi_spec,
					     &ipv6_multi_mask);
			if (ret)
				goto error;
		}
	}
	/* Add MAC address flows. */
	for (i = 0; i != MLX5_MAX_MAC_ADDRESSES; ++i) {
		struct ether_addr *mac = &dev->data->mac_addrs[i];

		if (!memcmp(mac, &cmp, sizeof(*mac)))
			continue;
		memcpy(&unicast.dst.addr_bytes,
		       mac->addr_bytes,
		       ETHER_ADDR_LEN);
		for (j = 0; j != vlan_filter_n; ++j) {
			uint16_t vlan = priv->vlan_filter[j];

			struct rte_flow_item_vlan vlan_spec = {
				.tci = rte_cpu_to_be_16(vlan),
			};
			struct rte_flow_item_vlan vlan_mask = {
				.tci = 0xffff,
			};

			ret = mlx5_ctrl_flow_vlan(dev, &unicast,
						  &unicast_mask,
						  &vlan_spec,
						  &vlan_mask);
			if (ret)
				goto error;
		}
		if (!vlan_filter_n) {
			ret = mlx5_ctrl_flow(dev, &unicast, &unicast_mask);
			if (ret)
				goto error;
		}
	}
	return 0;
error:
	ret = rte_errno; /* Save rte_errno before cleanup. */
	mlx5_flow_list_flush(dev, &priv->ctrl_flows);
	rte_errno = ret; /* Restore rte_errno. */
	return -rte_errno;
}


/**
 * Disable traffic flows configured by control plane
 *
 * @param dev
 *   Pointer to Ethernet device private data.
 */
void
mlx5_traffic_disable(struct rte_eth_dev *dev)
{
	struct priv *priv = dev->data->dev_private;

	mlx5_flow_list_flush(dev, &priv->ctrl_flows);
}

/**
 * Restart traffic flows configured by control plane
 *
 * @param dev
 *   Pointer to Ethernet device private data.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
int
mlx5_traffic_restart(struct rte_eth_dev *dev)
{
	if (dev->data->dev_started) {
		mlx5_traffic_disable(dev);
		return mlx5_traffic_enable(dev);
	}
	return 0;
}
