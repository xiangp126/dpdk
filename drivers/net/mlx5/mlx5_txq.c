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

#include <stddef.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

/* Verbs header. */
/* ISO C doesn't support unnamed structs/unions, disabling -pedantic. */
#ifdef PEDANTIC
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include <infiniband/verbs.h>
#ifdef PEDANTIC
#pragma GCC diagnostic error "-Wpedantic"
#endif

#include <rte_mbuf.h>
#include <rte_malloc.h>
#include <rte_ethdev.h>
#include <rte_common.h>

#include "mlx5_utils.h"
#include "mlx5_defs.h"
#include "mlx5.h"
#include "mlx5_rxtx.h"
#include "mlx5_autoconf.h"

/**
 * Allocate TX queue elements.
 *
 * @param txq_ctrl
 *   Pointer to TX queue structure.
 */
void
txq_alloc_elts(struct mlx5_txq_ctrl *txq_ctrl)
{
	const unsigned int elts_n = 1 << txq_ctrl->txq.elts_n;
	unsigned int i;

	for (i = 0; (i != elts_n); ++i)
		(*txq_ctrl->txq.elts)[i] = NULL;
	DRV_LOG(DEBUG, "port %u Tx queue %u allocated and configured %u WRs",
		PORT_ID(txq_ctrl->priv), txq_ctrl->idx, elts_n);
	txq_ctrl->txq.elts_head = 0;
	txq_ctrl->txq.elts_tail = 0;
	txq_ctrl->txq.elts_comp = 0;
}

/**
 * Free TX queue elements.
 *
 * @param txq_ctrl
 *   Pointer to TX queue structure.
 */
static void
txq_free_elts(struct mlx5_txq_ctrl *txq_ctrl)
{
	const uint16_t elts_n = 1 << txq_ctrl->txq.elts_n;
	const uint16_t elts_m = elts_n - 1;
	uint16_t elts_head = txq_ctrl->txq.elts_head;
	uint16_t elts_tail = txq_ctrl->txq.elts_tail;
	struct rte_mbuf *(*elts)[elts_n] = txq_ctrl->txq.elts;

	DRV_LOG(DEBUG, "port %u Tx queue %u freeing WRs",
		PORT_ID(txq_ctrl->priv), txq_ctrl->idx);
	txq_ctrl->txq.elts_head = 0;
	txq_ctrl->txq.elts_tail = 0;
	txq_ctrl->txq.elts_comp = 0;

	while (elts_tail != elts_head) {
		struct rte_mbuf *elt = (*elts)[elts_tail & elts_m];

		assert(elt != NULL);
		rte_pktmbuf_free_seg(elt);
#ifndef NDEBUG
		/* Poisoning. */
		memset(&(*elts)[elts_tail & elts_m],
		       0x77,
		       sizeof((*elts)[elts_tail & elts_m]));
#endif
		++elts_tail;
	}
}

/**
 * DPDK callback to configure a TX queue.
 *
 * @param dev
 *   Pointer to Ethernet device structure.
 * @param idx
 *   TX queue index.
 * @param desc
 *   Number of descriptors to configure in queue.
 * @param socket
 *   NUMA socket on which memory must be allocated.
 * @param[in] conf
 *   Thresholds parameters.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
int
mlx5_tx_queue_setup(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc,
		    unsigned int socket, const struct rte_eth_txconf *conf)
{
	struct priv *priv = dev->data->dev_private;
	struct mlx5_txq_data *txq = (*priv->txqs)[idx];
	struct mlx5_txq_ctrl *txq_ctrl =
		container_of(txq, struct mlx5_txq_ctrl, txq);

	if (desc <= MLX5_TX_COMP_THRESH) {
		DRV_LOG(WARNING,
			"port %u number of descriptors requested for Tx queue"
			" %u must be higher than MLX5_TX_COMP_THRESH, using %u"
			" instead of %u",
			dev->data->port_id, idx, MLX5_TX_COMP_THRESH + 1, desc);
		desc = MLX5_TX_COMP_THRESH + 1;
	}
	if (!rte_is_power_of_2(desc)) {
		desc = 1 << log2above(desc);
		DRV_LOG(WARNING,
			"port %u increased number of descriptors in Tx queue"
			" %u to the next power of two (%d)",
			dev->data->port_id, idx, desc);
	}
	DRV_LOG(DEBUG, "port %u configuring queue %u for %u descriptors",
		dev->data->port_id, idx, desc);
	if (idx >= priv->txqs_n) {
		DRV_LOG(ERR, "port %u Tx queue index out of range (%u >= %u)",
			dev->data->port_id, idx, priv->txqs_n);
		rte_errno = EOVERFLOW;
		return -rte_errno;
	}
	if (!mlx5_txq_releasable(dev, idx)) {
		rte_errno = EBUSY;
		DRV_LOG(ERR, "port %u unable to release queue index %u",
			dev->data->port_id, idx);
		return -rte_errno;
	}
	mlx5_txq_release(dev, idx);
	txq_ctrl = mlx5_txq_new(dev, idx, desc, socket, conf);
	if (!txq_ctrl) {
		DRV_LOG(ERR, "port %u unable to allocate queue index %u",
			dev->data->port_id, idx);
		return -rte_errno;
	}
	DRV_LOG(DEBUG, "port %u adding Tx queue %u to list",
		dev->data->port_id, idx);
	(*priv->txqs)[idx] = &txq_ctrl->txq;
	return 0;
}

/**
 * DPDK callback to release a TX queue.
 *
 * @param dpdk_txq
 *   Generic TX queue pointer.
 */
void
mlx5_tx_queue_release(void *dpdk_txq)
{
	struct mlx5_txq_data *txq = (struct mlx5_txq_data *)dpdk_txq;
	struct mlx5_txq_ctrl *txq_ctrl;
	struct priv *priv;
	unsigned int i;

	if (txq == NULL)
		return;
	txq_ctrl = container_of(txq, struct mlx5_txq_ctrl, txq);
	priv = txq_ctrl->priv;
	for (i = 0; (i != priv->txqs_n); ++i)
		if ((*priv->txqs)[i] == txq) {
			mlx5_txq_release(ETH_DEV(priv), i);
			DRV_LOG(DEBUG, "port %u removing Tx queue %u from list",
				PORT_ID(priv), txq_ctrl->idx);
			break;
		}
}


/**
 * Mmap TX UAR(HW doorbell) pages into reserved UAR address space.
 * Both primary and secondary process do mmap to make UAR address
 * aligned.
 *
 * @param[in] dev
 *   Pointer to Ethernet device.
 * @param fd
 *   Verbs file descriptor to map UAR pages.
 *
 * @return
 *   0 on success, a negative errno value otherwise and rte_errno is set.
 */
int
mlx5_tx_uar_remap(struct rte_eth_dev *dev, int fd)
{
	struct priv *priv = dev->data->dev_private;
	unsigned int i, j;
	uintptr_t pages[priv->txqs_n];
	unsigned int pages_n = 0;
	uintptr_t uar_va;
	uintptr_t off;
	void *addr;
	void *ret;
	struct mlx5_txq_data *txq;
	struct mlx5_txq_ctrl *txq_ctrl;
	int already_mapped;
	size_t page_size = sysconf(_SC_PAGESIZE);

	memset(pages, 0, priv->txqs_n * sizeof(uintptr_t));
	/*
	 * As rdma-core, UARs are mapped in size of OS page size.
	 * Use aligned address to avoid duplicate mmap.
	 * Ref to libmlx5 function: mlx5_init_context()
	 */
	for (i = 0; i != priv->txqs_n; ++i) {
		if (!(*priv->txqs)[i])
			continue;
		txq = (*priv->txqs)[i];
		txq_ctrl = container_of(txq, struct mlx5_txq_ctrl, txq);
		assert(txq_ctrl->idx == (uint16_t)i);
		/* UAR addr form verbs used to find dup and offset in page. */
		uar_va = (uintptr_t)txq_ctrl->bf_reg_orig;
		off = uar_va & (page_size - 1); /* offset in page. */
		uar_va = RTE_ALIGN_FLOOR(uar_va, page_size); /* page addr. */
		already_mapped = 0;
		for (j = 0; j != pages_n; ++j) {
			if (pages[j] == uar_va) {
				already_mapped = 1;
				break;
			}
		}
		/* new address in reserved UAR address space. */
		addr = RTE_PTR_ADD(priv->uar_base,
				   uar_va & (MLX5_UAR_SIZE - 1));
		if (!already_mapped) {
			pages[pages_n++] = uar_va;
			/* fixed mmap to specified address in reserved
			 * address space.
			 */
			ret = mmap(addr, page_size,
				   PROT_WRITE, MAP_FIXED | MAP_SHARED, fd,
				   txq_ctrl->uar_mmap_offset);
			if (ret != addr) {
				/* fixed mmap have to return same address */
				DRV_LOG(ERR,
					"port %u call to mmap failed on UAR"
					" for txq %u",
					dev->data->port_id, txq_ctrl->idx);
				rte_errno = ENXIO;
				return -rte_errno;
			}
		}
		if (rte_eal_process_type() == RTE_PROC_PRIMARY) /* save once */
			txq_ctrl->txq.bf_reg = RTE_PTR_ADD((void *)addr, off);
		else
			assert(txq_ctrl->txq.bf_reg ==
			       RTE_PTR_ADD((void *)addr, off));
	}
	return 0;
}

/**
 * Create the Tx queue Verbs object.
 *
 * @param dev
 *   Pointer to Ethernet device.
 * @param idx
 *   Queue index in DPDK Rx queue array
 *
 * @return
 *   The Verbs object initialised, NULL otherwise and rte_errno is set.
 */
struct mlx5_txq_ibv *
mlx5_txq_ibv_new(struct rte_eth_dev *dev, uint16_t idx)
{
	struct priv *priv = dev->data->dev_private;
	struct mlx5_txq_data *txq_data = (*priv->txqs)[idx];
	struct mlx5_txq_ctrl *txq_ctrl =
		container_of(txq_data, struct mlx5_txq_ctrl, txq);
	struct mlx5_txq_ibv tmpl;
	struct mlx5_txq_ibv *txq_ibv;
	union {
		struct ibv_qp_init_attr_ex init;
		struct ibv_cq_init_attr_ex cq;
		struct ibv_qp_attr mod;
		struct ibv_cq_ex cq_attr;
	} attr;
	unsigned int cqe_n;
	struct mlx5dv_qp qp = { .comp_mask = MLX5DV_QP_MASK_UAR_MMAP_OFFSET };
	struct mlx5dv_cq cq_info;
	struct mlx5dv_obj obj;
	const int desc = 1 << txq_data->elts_n;
	int ret = 0;

	assert(txq_data);
	priv->verbs_alloc_ctx.type = MLX5_VERBS_ALLOC_TYPE_TX_QUEUE;
	priv->verbs_alloc_ctx.obj = txq_ctrl;
	if (mlx5_getenv_int("MLX5_ENABLE_CQE_COMPRESSION")) {
		DRV_LOG(ERR,
			"port %u MLX5_ENABLE_CQE_COMPRESSION must never be set",
			dev->data->port_id);
		rte_errno = EINVAL;
		return NULL;
	}
	memset(&tmpl, 0, sizeof(struct mlx5_txq_ibv));
	/* MRs will be registered in mp2mr[] later. */
	attr.cq = (struct ibv_cq_init_attr_ex){
		.comp_mask = 0,
	};
	cqe_n = ((desc / MLX5_TX_COMP_THRESH) - 1) ?
		((desc / MLX5_TX_COMP_THRESH) - 1) : 1;
	if (priv->mps == MLX5_MPW_ENHANCED)
		cqe_n += MLX5_TX_COMP_THRESH_INLINE_DIV;
	tmpl.cq = ibv_create_cq(priv->ctx, cqe_n, NULL, NULL, 0);
	if (tmpl.cq == NULL) {
		DRV_LOG(ERR, "port %u Tx queue %u CQ creation failure",
			dev->data->port_id, idx);
		rte_errno = errno;
		goto error;
	}
	attr.init = (struct ibv_qp_init_attr_ex){
		/* CQ to be associated with the send queue. */
		.send_cq = tmpl.cq,
		/* CQ to be associated with the receive queue. */
		.recv_cq = tmpl.cq,
		.cap = {
			/* Max number of outstanding WRs. */
			.max_send_wr =
				((priv->device_attr.orig_attr.max_qp_wr <
				  desc) ?
				 priv->device_attr.orig_attr.max_qp_wr :
				 desc),
			/*
			 * Max number of scatter/gather elements in a WR,
			 * must be 1 to prevent libmlx5 from trying to affect
			 * too much memory. TX gather is not impacted by the
			 * priv->device_attr.max_sge limit and will still work
			 * properly.
			 */
			.max_send_sge = 1,
		},
		.qp_type = IBV_QPT_RAW_PACKET,
		/*
		 * Do *NOT* enable this, completions events are managed per
		 * Tx burst.
		 */
		.sq_sig_all = 0,
		.pd = priv->pd,
		.comp_mask = IBV_QP_INIT_ATTR_PD,
	};
	if (txq_data->inline_en)
		attr.init.cap.max_inline_data = txq_ctrl->max_inline_data;
	if (txq_data->tso_en) {
		attr.init.max_tso_header = txq_ctrl->max_tso_header;
		attr.init.comp_mask |= IBV_QP_INIT_ATTR_MAX_TSO_HEADER;
	}
	tmpl.qp = ibv_create_qp_ex(priv->ctx, &attr.init);
	if (tmpl.qp == NULL) {
		DRV_LOG(ERR, "port %u Tx queue %u QP creation failure",
			dev->data->port_id, idx);
		rte_errno = errno;
		goto error;
	}
	attr.mod = (struct ibv_qp_attr){
		/* Move the QP to this state. */
		.qp_state = IBV_QPS_INIT,
		/* Primary port number. */
		.port_num = priv->port
	};
	ret = ibv_modify_qp(tmpl.qp, &attr.mod, (IBV_QP_STATE | IBV_QP_PORT));
	if (ret) {
		DRV_LOG(ERR,
			"port %u Tx queue %u QP state to IBV_QPS_INIT failed",
			dev->data->port_id, idx);
		rte_errno = errno;
		goto error;
	}
	attr.mod = (struct ibv_qp_attr){
		.qp_state = IBV_QPS_RTR
	};
	ret = ibv_modify_qp(tmpl.qp, &attr.mod, IBV_QP_STATE);
	if (ret) {
		DRV_LOG(ERR,
			"port %u Tx queue %u QP state to IBV_QPS_RTR failed",
			dev->data->port_id, idx);
		rte_errno = errno;
		goto error;
	}
	attr.mod.qp_state = IBV_QPS_RTS;
	ret = ibv_modify_qp(tmpl.qp, &attr.mod, IBV_QP_STATE);
	if (ret) {
		DRV_LOG(ERR,
			"port %u Tx queue %u QP state to IBV_QPS_RTS failed",
			dev->data->port_id, idx);
		rte_errno = errno;
		goto error;
	}
	txq_ibv = rte_calloc_socket(__func__, 1, sizeof(struct mlx5_txq_ibv), 0,
				    txq_ctrl->socket);
	if (!txq_ibv) {
		DRV_LOG(ERR, "port %u Tx queue %u cannot allocate memory",
			dev->data->port_id, idx);
		rte_errno = ENOMEM;
		goto error;
	}
	obj.cq.in = tmpl.cq;
	obj.cq.out = &cq_info;
	obj.qp.in = tmpl.qp;
	obj.qp.out = &qp;
	ret = mlx5dv_init_obj(&obj, MLX5DV_OBJ_CQ | MLX5DV_OBJ_QP);
	if (ret != 0) {
		rte_errno = errno;
		goto error;
	}
	if (cq_info.cqe_size != RTE_CACHE_LINE_SIZE) {
		DRV_LOG(ERR,
			"port %u wrong MLX5_CQE_SIZE environment variable"
			" value: it should be set to %u",
			dev->data->port_id, RTE_CACHE_LINE_SIZE);
		rte_errno = EINVAL;
		goto error;
	}
	txq_data->cqe_n = log2above(cq_info.cqe_cnt);
	txq_data->qp_num_8s = tmpl.qp->qp_num << 8;
	txq_data->wqes = qp.sq.buf;
	txq_data->wqe_n = log2above(qp.sq.wqe_cnt);
	txq_data->qp_db = &qp.dbrec[MLX5_SND_DBR];
	txq_ctrl->bf_reg_orig = qp.bf.reg;
	txq_data->cq_db = cq_info.dbrec;
	txq_data->cqes =
		(volatile struct mlx5_cqe (*)[])
		(uintptr_t)cq_info.buf;
	txq_data->cq_ci = 0;
	txq_data->cq_pi = 0;
	txq_data->wqe_ci = 0;
	txq_data->wqe_pi = 0;
	txq_ibv->qp = tmpl.qp;
	txq_ibv->cq = tmpl.cq;
	rte_atomic32_inc(&txq_ibv->refcnt);
	if (qp.comp_mask & MLX5DV_QP_MASK_UAR_MMAP_OFFSET) {
		txq_ctrl->uar_mmap_offset = qp.uar_mmap_offset;
	} else {
		DRV_LOG(ERR,
			"port %u failed to retrieve UAR info, invalid"
			" libmlx5.so",
			dev->data->port_id);
		rte_errno = EINVAL;
		goto error;
	}
	DRV_LOG(DEBUG, "port %u Verbs Tx queue %u: refcnt %d",
		dev->data->port_id, idx, rte_atomic32_read(&txq_ibv->refcnt));
	LIST_INSERT_HEAD(&priv->txqsibv, txq_ibv, next);
	txq_ibv->txq_ctrl = txq_ctrl;
	priv->verbs_alloc_ctx.type = MLX5_VERBS_ALLOC_TYPE_NONE;
	return txq_ibv;
error:
	ret = rte_errno; /* Save rte_errno before cleanup. */
	if (tmpl.cq)
		claim_zero(ibv_destroy_cq(tmpl.cq));
	if (tmpl.qp)
		claim_zero(ibv_destroy_qp(tmpl.qp));
	priv->verbs_alloc_ctx.type = MLX5_VERBS_ALLOC_TYPE_NONE;
	rte_errno = ret; /* Restore rte_errno. */
	return NULL;
}

/**
 * Get an Tx queue Verbs object.
 *
 * @param dev
 *   Pointer to Ethernet device.
 * @param idx
 *   Queue index in DPDK Rx queue array
 *
 * @return
 *   The Verbs object if it exists.
 */
struct mlx5_txq_ibv *
mlx5_txq_ibv_get(struct rte_eth_dev *dev, uint16_t idx)
{
	struct priv *priv = dev->data->dev_private;
	struct mlx5_txq_ctrl *txq_ctrl;

	if (idx >= priv->txqs_n)
		return NULL;
	if (!(*priv->txqs)[idx])
		return NULL;
	txq_ctrl = container_of((*priv->txqs)[idx], struct mlx5_txq_ctrl, txq);
	if (txq_ctrl->ibv) {
		rte_atomic32_inc(&txq_ctrl->ibv->refcnt);
		DRV_LOG(DEBUG, "port %u Verbs Tx queue %u: refcnt %d",
			dev->data->port_id, txq_ctrl->idx,
		      rte_atomic32_read(&txq_ctrl->ibv->refcnt));
	}
	return txq_ctrl->ibv;
}

/**
 * Release an Tx verbs queue object.
 *
 * @param txq_ibv
 *   Verbs Tx queue object.
 *
 * @return
 *   1 while a reference on it exists, 0 when freed.
 */
int
mlx5_txq_ibv_release(struct mlx5_txq_ibv *txq_ibv)
{
	assert(txq_ibv);
	DRV_LOG(DEBUG, "port %u Verbs Tx queue %u: refcnt %d",
		PORT_ID(txq_ibv->txq_ctrl->priv),
		txq_ibv->txq_ctrl->idx, rte_atomic32_read(&txq_ibv->refcnt));
	if (rte_atomic32_dec_and_test(&txq_ibv->refcnt)) {
		claim_zero(ibv_destroy_qp(txq_ibv->qp));
		claim_zero(ibv_destroy_cq(txq_ibv->cq));
		LIST_REMOVE(txq_ibv, next);
		rte_free(txq_ibv);
		return 0;
	}
	return 1;
}

/**
 * Return true if a single reference exists on the object.
 *
 * @param txq_ibv
 *   Verbs Tx queue object.
 */
int
mlx5_txq_ibv_releasable(struct mlx5_txq_ibv *txq_ibv)
{
	assert(txq_ibv);
	return (rte_atomic32_read(&txq_ibv->refcnt) == 1);
}

/**
 * Verify the Verbs Tx queue list is empty
 *
 * @param dev
 *   Pointer to Ethernet device.
 *
 * @return
 *   The number of object not released.
 */
int
mlx5_txq_ibv_verify(struct rte_eth_dev *dev)
{
	struct priv *priv = dev->data->dev_private;
	int ret = 0;
	struct mlx5_txq_ibv *txq_ibv;

	LIST_FOREACH(txq_ibv, &priv->txqsibv, next) {
		DRV_LOG(DEBUG, "port %u Verbs Tx queue %u still referenced",
			dev->data->port_id, txq_ibv->txq_ctrl->idx);
		++ret;
	}
	return ret;
}

/**
 * Create a DPDK Tx queue.
 *
 * @param dev
 *   Pointer to Ethernet device.
 * @param idx
 *   TX queue index.
 * @param desc
 *   Number of descriptors to configure in queue.
 * @param socket
 *   NUMA socket on which memory must be allocated.
 * @param[in] conf
 *  Thresholds parameters.
 *
 * @return
 *   A DPDK queue object on success, NULL otherwise and rte_errno is set.
 */
struct mlx5_txq_ctrl *
mlx5_txq_new(struct rte_eth_dev *dev, uint16_t idx, uint16_t desc,
	     unsigned int socket, const struct rte_eth_txconf *conf)
{
	struct priv *priv = dev->data->dev_private;
	const unsigned int max_tso_inline =
		((MLX5_MAX_TSO_HEADER + (RTE_CACHE_LINE_SIZE - 1)) /
		 RTE_CACHE_LINE_SIZE);
	struct mlx5_txq_ctrl *tmpl;

	tmpl = rte_calloc_socket("TXQ", 1,
				 sizeof(*tmpl) +
				 desc * sizeof(struct rte_mbuf *),
				 0, socket);
	if (!tmpl) {
		rte_errno = ENOMEM;
		return NULL;
	}
	assert(desc > MLX5_TX_COMP_THRESH);
	tmpl->txq.flags = conf->txq_flags;
	tmpl->priv = priv;
	tmpl->socket = socket;
	tmpl->txq.elts_n = log2above(desc);
	tmpl->idx = idx;
	if (priv->mps == MLX5_MPW_ENHANCED)
		tmpl->txq.mpw_hdr_dseg = priv->mpw_hdr_dseg;
	/* MRs will be registered in mp2mr[] later. */
	DRV_LOG(DEBUG, "port %u priv->device_attr.max_qp_wr is %d",
		dev->data->port_id, priv->device_attr.orig_attr.max_qp_wr);
	DRV_LOG(DEBUG, "port %u priv->device_attr.max_sge is %d",
		dev->data->port_id, priv->device_attr.orig_attr.max_sge);
	if (priv->txq_inline && (priv->txqs_n >= priv->txqs_inline)) {
		unsigned int ds_cnt;

		tmpl->txq.max_inline =
			((priv->txq_inline + (RTE_CACHE_LINE_SIZE - 1)) /
			 RTE_CACHE_LINE_SIZE);
		tmpl->txq.inline_en = 1;
		/* TSO and MPS can't be enabled concurrently. */
		assert(!priv->tso || !priv->mps);
		if (priv->mps == MLX5_MPW_ENHANCED) {
			tmpl->txq.inline_max_packet_sz =
				priv->inline_max_packet_sz;
			/* To minimize the size of data set, avoid requesting
			 * too large WQ.
			 */
			tmpl->max_inline_data =
				((RTE_MIN(priv->txq_inline,
					  priv->inline_max_packet_sz) +
				  (RTE_CACHE_LINE_SIZE - 1)) /
				 RTE_CACHE_LINE_SIZE) * RTE_CACHE_LINE_SIZE;
		} else {
			tmpl->max_inline_data =
				tmpl->txq.max_inline * RTE_CACHE_LINE_SIZE;
		}
		/*
		 * Check if the inline size is too large in a way which
		 * can make the WQE DS to overflow.
		 * Considering in calculation:
		 *      WQE CTRL (1 DS)
		 *      WQE ETH  (1 DS)
		 *      Inline part (N DS)
		 */
		ds_cnt = 2 + (tmpl->txq.max_inline / MLX5_WQE_DWORD_SIZE);
		if (ds_cnt > MLX5_DSEG_MAX) {
			unsigned int max_inline = (MLX5_DSEG_MAX - 2) *
						  MLX5_WQE_DWORD_SIZE;

			max_inline = max_inline - (max_inline %
						   RTE_CACHE_LINE_SIZE);
			DRV_LOG(WARNING,
				"port %u txq inline is too large (%d) setting it"
				" to the maximum possible: %d\n",
				PORT_ID(priv), priv->txq_inline, max_inline);
			tmpl->txq.max_inline = max_inline / RTE_CACHE_LINE_SIZE;
		}
	}
	if (priv->tso) {
		tmpl->max_tso_header = max_tso_inline * RTE_CACHE_LINE_SIZE;
		tmpl->txq.max_inline = RTE_MAX(tmpl->txq.max_inline,
					       max_tso_inline);
		tmpl->txq.tso_en = 1;
	}
	if (priv->tunnel_en)
		tmpl->txq.tunnel_en = 1;
	tmpl->txq.elts =
		(struct rte_mbuf *(*)[1 << tmpl->txq.elts_n])(tmpl + 1);
	tmpl->txq.stats.idx = idx;
	rte_atomic32_inc(&tmpl->refcnt);
	DRV_LOG(DEBUG, "port %u Tx queue %u: refcnt %d", dev->data->port_id,
		idx, rte_atomic32_read(&tmpl->refcnt));
	LIST_INSERT_HEAD(&priv->txqsctrl, tmpl, next);
	return tmpl;
}

/**
 * Get a Tx queue.
 *
 * @param dev
 *   Pointer to Ethernet device.
 * @param idx
 *   TX queue index.
 *
 * @return
 *   A pointer to the queue if it exists.
 */
struct mlx5_txq_ctrl *
mlx5_txq_get(struct rte_eth_dev *dev, uint16_t idx)
{
	struct priv *priv = dev->data->dev_private;
	struct mlx5_txq_ctrl *ctrl = NULL;

	if ((*priv->txqs)[idx]) {
		ctrl = container_of((*priv->txqs)[idx], struct mlx5_txq_ctrl,
				    txq);
		unsigned int i;

		mlx5_txq_ibv_get(dev, idx);
		for (i = 0; i != MLX5_PMD_TX_MP_CACHE; ++i) {
			if (ctrl->txq.mp2mr[i])
				claim_nonzero
					(mlx5_mr_get(dev,
						     ctrl->txq.mp2mr[i]->mp));
		}
		rte_atomic32_inc(&ctrl->refcnt);
		DRV_LOG(DEBUG, "port %u Tx queue %u refcnt %d",
			dev->data->port_id,
			ctrl->idx, rte_atomic32_read(&ctrl->refcnt));
	}
	return ctrl;
}

/**
 * Release a Tx queue.
 *
 * @param dev
 *   Pointer to Ethernet device.
 * @param idx
 *   TX queue index.
 *
 * @return
 *   1 while a reference on it exists, 0 when freed.
 */
int
mlx5_txq_release(struct rte_eth_dev *dev, uint16_t idx)
{
	struct priv *priv = dev->data->dev_private;
	unsigned int i;
	struct mlx5_txq_ctrl *txq;
	size_t page_size = sysconf(_SC_PAGESIZE);

	if (!(*priv->txqs)[idx])
		return 0;
	txq = container_of((*priv->txqs)[idx], struct mlx5_txq_ctrl, txq);
	DRV_LOG(DEBUG, "port %u Tx queue %u: refcnt %d", dev->data->port_id,
		txq->idx, rte_atomic32_read(&txq->refcnt));
	if (txq->ibv && !mlx5_txq_ibv_release(txq->ibv))
		txq->ibv = NULL;
	for (i = 0; i != MLX5_PMD_TX_MP_CACHE; ++i) {
		if (txq->txq.mp2mr[i]) {
			mlx5_mr_release(txq->txq.mp2mr[i]);
			txq->txq.mp2mr[i] = NULL;
		}
	}
	if (priv->uar_base)
		munmap((void *)RTE_ALIGN_FLOOR((uintptr_t)txq->txq.bf_reg,
		       page_size), page_size);
	if (rte_atomic32_dec_and_test(&txq->refcnt)) {
		txq_free_elts(txq);
		LIST_REMOVE(txq, next);
		rte_free(txq);
		(*priv->txqs)[idx] = NULL;
		return 0;
	}
	return 1;
}

/**
 * Verify if the queue can be released.
 *
 * @param dev
 *   Pointer to Ethernet device.
 * @param idx
 *   TX queue index.
 *
 * @return
 *   1 if the queue can be released.
 */
int
mlx5_txq_releasable(struct rte_eth_dev *dev, uint16_t idx)
{
	struct priv *priv = dev->data->dev_private;
	struct mlx5_txq_ctrl *txq;

	if (!(*priv->txqs)[idx])
		return -1;
	txq = container_of((*priv->txqs)[idx], struct mlx5_txq_ctrl, txq);
	return (rte_atomic32_read(&txq->refcnt) == 1);
}

/**
 * Verify the Tx Queue list is empty
 *
 * @param dev
 *   Pointer to Ethernet device.
 *
 * @return
 *   The number of object not released.
 */
int
mlx5_txq_verify(struct rte_eth_dev *dev)
{
	struct priv *priv = dev->data->dev_private;
	struct mlx5_txq_ctrl *txq;
	int ret = 0;

	LIST_FOREACH(txq, &priv->txqsctrl, next) {
		DRV_LOG(DEBUG, "port %u Tx queue %u still referenced",
			dev->data->port_id, txq->idx);
		++ret;
	}
	return ret;
}
