/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * vchiq_rx.c - the receive side: walking the VPU's stream, its messages, the
 * VPU's sync slot, handing its slots back, our recycled slots, the doorbell
 * events and the polled wait.
 */
#include <vchiq/vchiq_core_priv.h>

#define CLOSE_ANSWER_US 100000u /* how long the walk waits for room to answer the VPU's CLOSE */

/* ---- the VPU's slots ----------------------------------------------------- */

/**
 * vq_recycle_slot() - Append a VPU slot we are done with to the VPU's recycle queue.
 * @ctx:  the context
 * @slot: the VPU slot's index
 *
 * The caller signals the VPU's recycle event.
 */
void vq_recycle_slot(struct vchiq_ctx *ctx, u32 slot)
{
	u32 recycle = le32(ctx->shared_vpu->slot_queue_recycle);
	ctx->shared_vpu->slot_queue[recycle & VCHIQ_SLOT_QUEUE_MASK] = le32(slot);
	emu68_barrier(); /* the entry before the index that publishes it */
	ctx->shared_vpu->slot_queue_recycle = le32(recycle + 1u);
	ctx->stats.slots_recycled_out++;
}

/**
 * vq_release_slot() - Release one claim on a VPU slot; hand the slot back once every claim on it is released.
 * @ctx:  the context
 * @slot: the VPU slot's index
 * @hdr:  the message whose claim is released (a second release does nothing), or NULL for the walk's own claim on
 *        the slot
 *
 * The per-slot counters are ours, the recycle queue is the VPU's.
 */
void vq_release_slot(struct vchiq_ctx *ctx, u32 slot, volatile struct vchiq_header *hdr)
{
	if (hdr != NULL)
	{
		u32 msgid = le32(hdr->msgid);
		if ((msgid & VCHIQ_MSGID_CLAIMED) == 0)
			return; /* already released */
		hdr->msgid = le32(msgid & ~VCHIQ_MSGID_CLAIMED);
	}

	volatile struct vchiq_slot_claims *info = &ctx->host->slot_info[slot];
	info->release_count = (u16)(info->release_count + 1u);
	if (info->release_count == info->use_count)
	{
		vq_recycle_slot(ctx, slot);
		vq_remote_event_signal(ctx, &ctx->shared_vpu->recycle, FALSE);
	}
}

/**
 * vq_release_message_sync() - Free the VPU's sync slot again: the VPU may send the next message.
 * @ctx: the context
 */
void vq_release_message_sync(struct vchiq_ctx *ctx)
{
	sync_header(ctx, ctx->shared_vpu)->msgid = le32(VCHIQ_MSGID_PADDING);
	ctx->sync_held = 0;
	vq_remote_event_signal(ctx, &ctx->shared_vpu->sync_release, FALSE);
}

/* ---- messages ------------------------------------------------------------ */

/**
 * find_receiver() - The service DATA from the VPU goes to.
 * @ctx:     the context
 * @dstport: our port
 * @srcport: the VPU's port
 *
 * Return: the service, or NULL: no open service for it (unexpected), or the session is quiet (repair or shutdown:
 * nobody will read it, dropped).
 */
static struct vchiq_service *find_receiver(struct vchiq_ctx *ctx, u32 dstport, u32 srcport)
{
	struct vchiq_service *svc = vq_find_peer_service(ctx, dstport, srcport);
	if (svc == NULL || svc->state != VCHIQ_SVC_OPEN)
	{
		ctx->stats.unexpected_msgs++;
		return NULL;
	}
	if (ctx->quiet)
	{
		ctx->stats.dropped_msgs++;
		return NULL;
	}
	return svc;
}

/**
 * queue_for_client() - Put a message on a service's queue for its client.
 * @ctx: the context
 * @svc: the service
 * @hdr: the message
 *
 * Return: FALSE if the queue is full: the message stays where it is.
 */
static BOOL queue_for_client(struct vchiq_ctx *ctx, struct vchiq_service *svc, volatile struct vchiq_header *hdr)
{
	if (svc->rxq_tail - svc->rxq_head >= VCHIQ_RXQ_SIZE)
	{
		ctx->stats.rx_stalls++;
		return FALSE;
	}
	svc->rxq[svc->rxq_tail % VCHIQ_RXQ_SIZE] = hdr;
	svc->rxq_tail++;
	return TRUE;
}

/**
 * parse_openack() - An OPENACK: the VPU accepted our OPEN.
 * @ctx:     the context
 * @hdr:     the message; its payload carries the VPU's version
 * @size:    the payload's size in bytes
 * @dstport: our port
 * @srcport: the VPU's port for the service
 * @sync:    it came through the VPU's sync slot: the service is synchronous
 *
 * Unexpected unless a service on @dstport has its OPEN unanswered.
 */
static void parse_openack(struct vchiq_ctx *ctx, volatile struct vchiq_header *hdr, u32 size, u32 dstport, u32 srcport,
                          BOOL sync)
{
	struct vchiq_service *svc = vq_find_service_by_port(ctx, dstport);
	if (svc != NULL && is_open_pending(svc))
		vq_set_service_open(ctx, svc, srcport, hdr, size, sync);
	else
		ctx->stats.unexpected_msgs++;
}

/**
 * parse_close() - A CLOSE: the VPU refused our OPEN, answered our CLOSE, or closes an open service.
 * @ctx:     the context
 * @dstport: our port
 * @srcport: the VPU's port
 *
 * The VPU's close of an OPEN service is answered in kind and reported to the service's callback.  The answer is
 * queued inside the walk, so without polling: it waits up to CLOSE_ANSWER_US for room.
 */
static void parse_close(struct vchiq_ctx *ctx, u32 dstport, u32 srcport)
{
	struct vchiq_service *svc = vq_find_service_by_port(ctx, dstport);
	switch (svc != NULL ? svc->state : VCHIQ_SVC_FREE)
	{
	case VCHIQ_SVC_OPENING: /* the open was refused, or abandoned and our CLOSE answered */
	case VCHIQ_SVC_CLOSING: /* our CLOSE, answered */
		vq_close_service_complete(ctx, svc);
		break;
	case VCHIQ_SVC_OPEN: /* the VPU closes: answer in kind; inside the walk, so without polling */
		vq_close_service_complete(ctx, svc);
		(void)vq_queue_message(ctx, VCHIQ_MAKE_MSG(VCHIQ_MSG_CLOSE, svc->localport, srcport), NULL, 0, CLOSE_ANSWER_US,
		                       FALSE);
		vq_make_service_callback(ctx, svc, VCHIQ_REASON_SERVICE_CLOSED, NULL, 0);
		break;
	default: /* no service, or one already CLOSED */
		ctx->stats.unexpected_msgs++;
		break;
	}
}

/**
 * parse_data() - DATA in the VPU's stream: queued for its service's client.
 * @ctx:   the context
 * @hdr:   the message
 * @msgid: its msgid, in host order
 *
 * The message is held in the service's queue, its slot claimed, until the client releases it.  One with no
 * receiver is passed over.
 *
 * Return: FALSE if the queue is full: the walk waits.
 */
static BOOL parse_data(struct vchiq_ctx *ctx, volatile struct vchiq_header *hdr, u32 msgid)
{
	struct vchiq_service *svc = find_receiver(ctx, VCHIQ_MSG_DSTPORT(msgid), VCHIQ_MSG_SRCPORT(msgid));
	if (svc == NULL)
		return TRUE;
	if (!queue_for_client(ctx, svc, hdr))
		return FALSE;
	hdr->msgid = le32(msgid | VCHIQ_MSGID_CLAIMED);
	volatile struct vchiq_slot_claims *info = &ctx->host->slot_info[ctx->host->rx_slot];
	info->use_count = (u16)(info->use_count + 1u);
	return TRUE;
}

/**
 * parse_bulk_done() - BULK_TX_DONE or BULK_RX_DONE: the VPU finished, or aborted, the oldest bulk of that direction.
 * @ctx:     the context
 * @hdr:     the message; its payload is the bytes moved, or VCHIQ_BULK_ACTUAL_ABORTED
 * @size:    the payload's size in bytes
 * @dstport: our port
 * @srcport: the VPU's port
 * @dir:     VCHIQ_BULK_TX or VCHIQ_BULK_RX
 *
 * Taken for a CLOSING service too: its bulks are still ours.
 */
static void parse_bulk_done(struct vchiq_ctx *ctx, volatile struct vchiq_header *hdr, u32 size, u32 dstport, u32 srcport,
                            u32 dir)
{
	struct vchiq_service *svc = vq_find_peer_service(ctx, dstport, srcport); /* also while CLOSING: the bulks are still ours */
	struct vchiq_bulk *bulk = svc != NULL && size >= sizeof(__le32) ? vq_pop_bulk(svc, dir) : NULL;
	if (bulk == NULL)
	{
		ctx->stats.unexpected_msgs++;
		return;
	}
	s32 actual = (s32)le32(*(const volatile __le32 *)hdr->data); /* bytes moved, or VCHIQ_BULK_ACTUAL_ABORTED */
	ctx->stats.bulks_done++;
	vq_finish_bulk(ctx, svc, dir, bulk, actual);
}

/**
 * parse_message() - One message of the VPU's stream.
 * @ctx:   the context
 * @hdr:   the message
 * @msgid: its msgid, in host order
 * @size:  its payload's size in bytes
 *
 * A service whose queue is full holds up the walk until its client takes a message; nothing before that check
 * changes any state.
 *
 * Return: FALSE leaves the message where it is.
 */
static BOOL parse_message(struct vchiq_ctx *ctx, volatile struct vchiq_header *hdr, u32 msgid, u32 size)
{
	u32 type = VCHIQ_MSG_TYPE(msgid);
	u32 dstport = VCHIQ_MSG_DSTPORT(msgid);
	u32 srcport = VCHIQ_MSG_SRCPORT(msgid);

	KprintfT("[vchiq] rx type %ld %ld<-%ld size %ld (rx_pos %ld)\n", (ULONG)type, (ULONG)dstport, (ULONG)srcport,
	         (ULONG)size, (ULONG)ctx->host->rx_pos);

	switch (type)
	{
	case VCHIQ_MSG_PADDING: /* the rest of a slot */
		break;
	case VCHIQ_MSG_CONNECT: /* the VPU's answer to ours */
		ctx->conn_state = VCHIQ_CONN_CONNECTED;
		ctx->host->version_common = le16(ctx->slot_zero->version); /* the VPU may have lowered it */
		ctx->host->flags |= VCHIQ_HS_CONNECTED;
		break;
	case VCHIQ_MSG_OPENACK:
		parse_openack(ctx, hdr, size, dstport, srcport, FALSE);
		break;
	case VCHIQ_MSG_CLOSE:
		parse_close(ctx, dstport, srcport);
		break;
	case VCHIQ_MSG_DATA:
		return parse_data(ctx, hdr, msgid);
	case VCHIQ_MSG_BULK_TX_DONE:
	case VCHIQ_MSG_BULK_RX_DONE:
		parse_bulk_done(ctx, hdr, size, dstport, srcport, type == VCHIQ_MSG_BULK_TX_DONE ? VCHIQ_BULK_TX : VCHIQ_BULK_RX);
		break;
	default: /* PAUSE, RESUME, REMOTE_USE* (we never suspend the VPU), an OPEN from the VPU (we serve none) */
		ctx->stats.unexpected_msgs++;
		break;
	}
	return TRUE;
}

/**
 * enter_message() - The header of the message at rx_pos, entering its slot when rx_pos starts one.
 * @ctx: the context
 *
 * Entering a slot takes the walk's own claim on it, released at the end of the slot.
 *
 * Return: the header, or NULL, the session marked dirty, if the stream is corrupt.
 */
static volatile struct vchiq_header *enter_message(struct vchiq_ctx *ctx)
{
	volatile struct vchiq_host_state *host = ctx->host;
	if (host->rx_slot == VCHIQ_SLOT_NONE)
	{
		u32 slot = stream_slot(ctx->shared_vpu, host->rx_pos);
		if (slot >= VCHIQ_MAX_SLOTS)
		{
			vchiq_mark_dirty(ctx); /* a slot index the format cannot name */
			return NULL;
		}
		host->rx_slot = slot;
		host->slot_info[slot].use_count = 1;
		host->slot_info[slot].release_count = 0;
	}
	volatile struct vchiq_header *hdr = header_at(ctx, host->rx_slot, host->rx_pos);
	if (VCHIQ_SLOT_OFFSET(host->rx_pos) + VCHIQ_MSG_STRIDE(le32(hdr->size)) > VCHIQ_SLOT_SIZE)
	{
		vchiq_mark_dirty(ctx); /* a message that does not fit its slot */
		return NULL;
	}
	return hdr;
}

/**
 * leave_message() - Step rx_pos past a message; at the end of its slot, leave the slot.
 * @ctx:  the context
 * @size: the message's payload size in bytes
 *
 * Leaving the slot releases the walk's claim on it.
 */
static void leave_message(struct vchiq_ctx *ctx, u32 size)
{
	volatile struct vchiq_host_state *host = ctx->host;
	host->rx_pos += VCHIQ_MSG_STRIDE(size);
	if (VCHIQ_SLOT_OFFSET(host->rx_pos) != 0)
		return;
	u32 done = host->rx_slot;
	host->rx_slot = VCHIQ_SLOT_NONE;
	vq_release_slot(ctx, done, NULL);
}

/**
 * parse_rx_slots() - Walk the VPU's stream from rx_pos to its published tx_pos.
 * @ctx: the context
 *
 * Stops early on a corrupt stream (the session is dirty) and at a stalled message, which comes round again on the
 * next poll.
 */
static void parse_rx_slots(struct vchiq_ctx *ctx)
{
	u32 tx_pos = le32(ctx->shared_vpu->tx_pos);
	while (ctx->host->rx_pos != tx_pos)
	{
		volatile struct vchiq_header *hdr = enter_message(ctx);
		if (hdr == NULL)
			return; /* corrupt: the session is dirty */
		u32 size = le32(hdr->size);
		if (!parse_message(ctx, hdr, le32(hdr->msgid), size))
			return; /* stalled: the same message comes round again on the next poll */
		ctx->stats.msgs_rx++;
		leave_message(ctx, size);
	}
}

/**
 * parse_sync_data() - DATA in the VPU's sync slot: queued for a synchronous service's client.
 * @ctx:     the context
 * @hdr:     the message
 * @size:    its payload's size in bytes
 * @dstport: our port
 * @srcport: the VPU's port
 *
 * Return: FALSE leaves it there: held for the client until vchiq_release_message(), or, the service's queue full,
 * for the next poll.  TRUE: nobody takes it (no receiver, not a synchronous service, too long).
 */
static BOOL parse_sync_data(struct vchiq_ctx *ctx, volatile struct vchiq_header *hdr, u32 size, u32 dstport, u32 srcport)
{
	struct vchiq_service *svc = find_receiver(ctx, dstport, srcport);
	if (svc == NULL)
		return TRUE;
	if (!svc->sync || size > VCHIQ_MAX_MSG_SIZE)
	{
		ctx->stats.unexpected_msgs++;
		return TRUE;
	}
	if (queue_for_client(ctx, svc, hdr))
	{
		ctx->sync_held = 1;
		ctx->stats.msgs_rx++;
	}
	return FALSE;
}

/**
 * parse_sync_message() - The message in the VPU's sync slot.
 * @ctx:   the context
 * @hdr:   the message
 * @msgid: its msgid, in host order
 * @size:  its payload's size in bytes
 *
 * Return: FALSE leaves it there (parse_sync_data()).
 */
static BOOL parse_sync_message(struct vchiq_ctx *ctx, volatile struct vchiq_header *hdr, u32 msgid, u32 size)
{
	u32 type = VCHIQ_MSG_TYPE(msgid);
	u32 dstport = VCHIQ_MSG_DSTPORT(msgid);
	u32 srcport = VCHIQ_MSG_SRCPORT(msgid);

	KprintfT("[vchiq] sync rx type %ld %ld<-%ld size %ld\n", (ULONG)type, (ULONG)dstport, (ULONG)srcport, (ULONG)size);

	switch (type)
	{
	case VCHIQ_MSG_OPENACK: /* the VPU makes the service synchronous */
		parse_openack(ctx, hdr, size, dstport, srcport, TRUE);
		return TRUE;
	case VCHIQ_MSG_DATA:
		return parse_sync_data(ctx, hdr, size, dstport, srcport);
	default:
		ctx->stats.unexpected_msgs++;
		return TRUE;
	}
}

/**
 * parse_sync_slot() - Take the message in the VPU's synchronous channel, if there is one.
 * @ctx: the context
 *
 * The VPU's sync slot holds one message, not PADDING, until we release it, which lets the VPU send the next.
 */
static void parse_sync_slot(struct vchiq_ctx *ctx)
{
	if (ctx->sync_held)
		return; /* the one message is still with a client */
	volatile struct vchiq_header *hdr = sync_header(ctx, ctx->shared_vpu);
	u32 msgid = le32(hdr->msgid);
	if (msgid == VCHIQ_MSGID_PADDING)
		return; /* empty */
	if (!parse_sync_message(ctx, hdr, msgid, le32(hdr->size)))
		return; /* held, or left for the next poll */
	ctx->stats.msgs_rx++;
	vq_release_message_sync(ctx);
}

/* ---- our slots ----------------------------------------------------------- */

/**
 * vq_process_free_queue() - Take in our slots the VPU has recycled.
 * @ctx: the context
 *
 * shared_ours->slot_queue up to its recycle index is ours to send from.
 */
void vq_process_free_queue(struct vchiq_ctx *ctx)
{
	u32 recycle = le32(ctx->shared_ours->slot_queue_recycle);
	ctx->stats.slots_recycled_in += recycle - ctx->host->slot_queue_available;
	ctx->host->slot_queue_available = recycle;
}

/* ---- the poll and the doorbell events ------------------------------------ */

/**
 * ack_event() - Clear an event's fired flag if it is set.
 * @event: one of our events
 */
static void ack_event(volatile struct vchiq_remote_event *event)
{
	if (le32(event->fired))
		event->fired = le32(0);
}

/**
 * vchiq_poll() - Do what the VPU asked for.
 * @ctx: the context
 *
 * Acknowledges the events, takes back our slots, walks the VPU's stream and its sync slot.
 */
void vchiq_poll(struct vchiq_ctx *ctx)
{
	/* acknowledge first: a signal that lands during the walk stays pending.  sync_release is a token, not a wake-up. */
	ack_event(&ctx->shared_ours->trigger);
	ack_event(&ctx->shared_ours->recycle);
	ack_event(&ctx->shared_ours->sync_trigger);
	/* our sync slot came free: the waiting sender retries in this round, or waits again */
	if (ctx->sync_tx_waiting && le32(ctx->shared_ours->sync_release.fired))
		ctx->sync_tx_waiting = 0;

	vq_process_free_queue(ctx);
	parse_rx_slots(ctx);
	parse_sync_slot(ctx);
}

/**
 * set_armed() - Set our events' armed flags.
 * @ctx:          the context
 * @armed:        1 or 0, for trigger, recycle and sync_trigger
 * @sync_release: 1 or 0, for sync_release
 */
static void set_armed(struct vchiq_ctx *ctx, u32 armed, u32 sync_release)
{
	ctx->shared_ours->trigger.armed = le32(armed);
	ctx->shared_ours->recycle.armed = le32(armed);
	ctx->shared_ours->sync_trigger.armed = le32(armed);
	ctx->shared_ours->sync_release.armed = le32(sync_release);
	emu68_barrier();
}

/**
 * vchiq_arm_events() - Ask the VPU to ring the doorbell.
 * @ctx: the context
 *
 * Armed: the next message (trigger, sync_trigger), the next recycled slot, and, while a sync send waits, our sync
 * slot coming free.
 */
void vchiq_arm_events(struct vchiq_ctx *ctx)
{
	set_armed(ctx, 1u, ctx->sync_tx_waiting);
}

/**
 * vchiq_disarm_events() - No doorbells wanted.
 * @ctx: the context
 *
 * The caller is about to walk, or nobody is left to serve them.
 */
void vchiq_disarm_events(struct vchiq_ctx *ctx)
{
	set_armed(ctx, 0u, 0u);
}

/**
 * vchiq_has_fired() - Whether something fired since the last vchiq_poll().
 * @ctx: the context
 *
 * Checked after arming, it closes the arm/fire race.
 *
 * Return: TRUE if an event the caller armed has fired.
 */
BOOL vchiq_has_fired(const struct vchiq_ctx *ctx)
{
	return le32(ctx->shared_ours->trigger.fired) != 0 || le32(ctx->shared_ours->recycle.fired) != 0 ||
	       le32(ctx->shared_ours->sync_trigger.fired) != 0 ||
	       (ctx->sync_tx_waiting && le32(ctx->shared_ours->sync_release.fired) != 0);
}

/**
 * vchiq_has_unwalked() - Whether the VPU has published messages the walk has not reached.
 * @ctx: the context
 *
 * Return: TRUE if so; FALSE without a connected session.
 */
BOOL vchiq_has_unwalked(const struct vchiq_ctx *ctx)
{
	return ctx->conn_state == VCHIQ_CONN_CONNECTED && ctx->host->rx_pos != le32(ctx->shared_vpu->tx_pos);
}

/**
 * vq_poll_until() - Poll the stream until a condition holds.  The blocking helpers' wait.
 * @ctx:        the context
 * @done:       the condition, checked after every poll
 * @arg:        passed to @done
 * @timeout_us: how long to wait
 *
 * Return: 0, VQERR_NOCONNECTION once the session goes dirty, VQERR_TIMEOUT after @timeout_us.
 */
s32 vq_poll_until(struct vchiq_ctx *ctx, BOOL (*done)(const struct vchiq_ctx *, const void *), const void *arg,
                  u32 timeout_us)
{
	u32 deadline = get_time() + timeout_us;
	for (;;)
	{
		vchiq_poll(ctx);
		if (done(ctx, arg))
			return 0;
		if (ctx->host->flags & VCHIQ_HS_DIRTY)
			return VQERR_NOCONNECTION;
		if (time_deadline_passed(get_time(), deadline))
			return VQERR_TIMEOUT;
		wait_step(ctx);
	}
}
