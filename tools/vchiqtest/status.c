/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * status.c - STATUS: vchiq.device's connection and counters (NSCMD_VQ_GETINFO).
 * The same facts VCHIQInfo COUNTERS prints for people, as key=value.
 */
#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#else
#include <proto/exec.h>
#endif

#include <dos/dos.h>
#include <exec/errors.h>

#include <bits.h>
#include <emu68test/kv.h>
#include <timing.h>

#include "commands.h"

const char status_template[] = "";

/* vi_Bringup's values, by number */
static const char *const bringups[] = {"fresh", "resumed", "repaired", "reinit"};

/**
 * do_status() - STATUS: the connection's state and counters.
 * @t:    the tool
 * @args: none
 *
 * Return: RETURN_OK; RETURN_ERROR when vchiq.device did not open; RETURN_FAIL when it did not answer.
 */
int do_status(struct tool *t, LONG *args)
{
	(void)args;
	struct vq_service *s = &t->svc;
	if (!vq_open(s, &t->waiter.timer, 0, 0, 0, NULL))
	{
		kv_num("vchiq.error", s->error);
		kv_str("fail", s->error == IOERR_OPENFAIL ? "vchiq.device did not load" : "no link to the VideoCore");
		return RETURN_ERROR;
	}
	struct Library *device = &s->io->iovq_Req.io_Device->dd_Library;
	kv_version("vchiq.device", device->lib_Version, device->lib_Revision);
	struct VchiqInfo info;
	BOOL ok = vq_get_info(s, &info);
	vq_close(s);
	if (!ok)
	{
		kv_str("fail", "NSCMD_VQ_GETINFO failed");
		return RETURN_FAIL;
	}
	kv_hex("vchiq.window", info.vi_WindowBase);
	kv_unum("vchiq.window_bytes", info.vi_WindowSize);
	kv_unum("vchiq.session", info.vi_Sessions);
	kv_unum("vchiq.version", info.vi_VersionCommon);
	kv_hex("vchiq.flags", info.vi_Flags);
	kv_unum("timer.now", get_time());
	kv_unum("vchiq.services", info.vi_Services);
	kv_unum("vchiq.rx_pos", info.vi_RxPos);
	kv_unum("vchiq.tx_pos", info.vi_TxPos);
	kv_unum("vchiq.msgs_tx", info.vi_MsgsTx);
	kv_unum("vchiq.msgs_rx", info.vi_MsgsRx);
	kv_unum("vchiq.slots_in", info.vi_SlotsIn);
	kv_unum("vchiq.slots_out", info.vi_SlotsOut);
	kv_unum("vchiq.bell_rings", info.vi_BellRings);
	kv_unum("vchiq.interrupts", info.vi_Interrupts);
	kv_unum("vchiq.bulks", info.vi_BulksDone);
	kv_unum("vchiq.unexpected", info.vi_Unexpected);
	kv_unum("vchiq.dropped", info.vi_Dropped);
	kv_unum("vchiq.stalls", info.vi_RxStalls);
	kv_str("vchiq.bringup", info.vi_Bringup < ARRAY_SIZE(bringups) ? bringups[info.vi_Bringup] : "?");
	kv_hex("vchiq.reach.start", info.vi_ReachStart);
	kv_hex("vchiq.reach.end", info.vi_ReachEnd);
	kv_hex("vchiq.reach.bus", info.vi_ReachBus);
	kv_hex("vchiq.uncached.start", info.vi_UncachedStart);
	kv_hex("vchiq.uncached.end", info.vi_UncachedEnd);
	return RETURN_OK;
}
