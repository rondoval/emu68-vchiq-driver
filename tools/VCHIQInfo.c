/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * VCHIQInfo - whether the link to the VideoCore is set up: the check to run
 * after installing vchiq.device, and what a bug report needs.
 *
 *   VCHIQInfo                         the setup in plain words, each part OK
 *                                     or a warning with its cure: Emu68's
 *                                     resources, the VCHIQ node, CMA,
 *                                     vchiq.device and how it came up, the
 *                                     firmware, its temperature, throttling,
 *                                     clocks and gpu_mem
 *   VCHIQInfo GCMD command            one gencmd command and its answer, as
 *                                     vcgencmd gives it
 *   VCHIQInfo COUNTERS                the connection's state and counters
 *   VCHIQInfo DUMP                    the shared window's state, read only
 *
 * One command per run; each ends with PASS or FAIL.
 *
 * Hosted CLI program (libnix).  A VCHIQ service is plain Exec I/O
 * (vchiq_client.c).
 *
 * Return codes: 0 PASS, 5 PASS with warnings, 10 a part is not installed,
 * 20 failed.
 */
#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#include <clib/dos_protos.h>
#else
#include <proto/exec.h>
#include <proto/dos.h>
#endif

#include <exec/types.h>
#include <exec/errors.h>
#include <dos/dos.h>
#include <dos/rdargs.h>

#include <stdlib.h>
#include <string.h>

#include <bits.h>
#include <devtree.h>
#include <drv_timer.h>
#include <strutil.h>
#include <timing.h>
#include <devices/vchiq.h>
#include <vchiq/vchiq_core.h> /* the window layout, read-only, for DUMP */

#include "vchiq_client.h"

APTR DeviceTreeBase;

static const char verstag[] __attribute__((used)) = VERSTAG;

static struct drv_timer timer;
static struct vq_service svc; /* big: the message buffers */

/* ---- the shared window ------------------------------------------------------- */

/**
 * dump_window() - The shared window's state, read straight from it (read-only: it is mapped and non-cacheable).
 * @info: the device's state, for the window's address
 * @when: what the dump is for, printed
 *
 * After a missing reply:
 *   VPU tx_pos != our rx_pos   the reply is in the ring; the device did not process it
 *   VPU trigger fired = 1      the VPU has not looked at the ring since we signalled it
 *   neither                    the VPU consumed the request and did not answer
 */
static void dump_window(const struct VchiqInfo *info, const char *when)
{
	volatile struct vchiq_host_state *host = (volatile struct vchiq_host_state *)info->vi_WindowBase;
	volatile struct vchiq_slot_zero *slot_zero =
	    (volatile struct vchiq_slot_zero *)(info->vi_WindowBase + host->slot_zero_offset);
	volatile struct vchiq_shared_state *m = &slot_zero->master;
	volatile struct vchiq_shared_state *s = &slot_zero->slave;

	Printf((CONST_STRPTR) "  dump (%s): device interrupts %ld, bell rings %ld, msgs tx %ld rx %ld\n", when, info->vi_Interrupts,
	    info->vi_BellRings, info->vi_MsgsTx, info->vi_MsgsRx);
	Printf((CONST_STRPTR) "    host: flags 0x%lx rx_pos %ld (slot %ld) tx_pos %ld (slot %ld) slots avail %ld\n", host->flags,
	    host->rx_pos, host->rx_slot, host->tx_pos, host->tx_slot, host->slot_queue_available);
	Printf((CONST_STRPTR) "    VPU:  tx_pos %ld, trigger a/f %ld/%ld, recycle a/f %ld/%ld, recycle idx %ld\n",
	    VCHIQ_LE32(m->tx_pos), VCHIQ_LE32(m->trigger.armed), VCHIQ_LE32(m->trigger.fired), VCHIQ_LE32(m->recycle.armed),
	    VCHIQ_LE32(m->recycle.fired), VCHIQ_LE32(m->slot_queue_recycle));
	Printf((CONST_STRPTR) "    ours: tx_pos %ld, trigger a/f %ld/%ld, recycle a/f %ld/%ld, recycle idx %ld\n",
	    VCHIQ_LE32(s->tx_pos), VCHIQ_LE32(s->trigger.armed), VCHIQ_LE32(s->trigger.fired), VCHIQ_LE32(s->recycle.armed),
	    VCHIQ_LE32(s->recycle.fired), VCHIQ_LE32(s->slot_queue_recycle));
	Printf((CONST_STRPTR) "    VPU debug[] %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld\n", VCHIQ_LE32(m->debug[0]),
	    VCHIQ_LE32(m->debug[1]), VCHIQ_LE32(m->debug[2]), VCHIQ_LE32(m->debug[3]), VCHIQ_LE32(m->debug[4]),
	    VCHIQ_LE32(m->debug[5]), VCHIQ_LE32(m->debug[6]), VCHIQ_LE32(m->debug[7]), VCHIQ_LE32(m->debug[8]),
	    VCHIQ_LE32(m->debug[9]), VCHIQ_LE32(m->debug[10]));
}

/**
 * do_dump() - DUMP: the shared window's state, through an information-only unit.
 *
 * Return: RETURN_OK, RETURN_FAIL if the device did not open or answer.
 */
static int do_dump(void)
{
	if (!vq_open(&svc, &timer, 0, 0, 0, "vchiq.device"))
		return RETURN_FAIL;
	struct VchiqInfo info;
	BOOL ok = vq_get_info(&svc, &info);
	vq_close(&svc);
	if (!ok)
	{
		Printf((CONST_STRPTR) "NSCMD_VQ_GETINFO failed\n");
		return RETURN_FAIL;
	}
	dump_window(&info, "now");
	return RETURN_OK;
}

/* ---- COUNTERS ------------------------------------------------------------------ */

/**
 * do_counters() - COUNTERS: the connection's state and counters.
 *
 * Return: RETURN_OK, RETURN_FAIL if the device did not open or answer.
 */
static int do_counters(void)
{
	if (!vq_open(&svc, &timer, 0, 0, 0, "vchiq.device"))
		return RETURN_FAIL;
	struct VchiqInfo info;
	BOOL ok = vq_get_info(&svc, &info);
	vq_close(&svc);
	if (!ok)
	{
		Printf((CONST_STRPTR) "NSCMD_VQ_GETINFO failed\n");
		return RETURN_FAIL;
	}
	Printf((CONST_STRPTR) "window 0x%08lx (%ld KiB), session %ld, VCHIQ version %ld, flags 0x%lx, %ld services open\n",
	    info.vi_WindowBase, info.vi_WindowSize / 1024u, info.vi_Sessions, info.vi_VersionCommon, info.vi_Flags, info.vi_Services);
	Printf((CONST_STRPTR) "positions: rx %ld, tx %ld; messages tx %ld rx %ld; slots in %ld out %ld; bell rings %ld, interrupts %ld; bulks %ld; unexpected %ld, dropped %ld, stalls %ld\n",
	    info.vi_RxPos, info.vi_TxPos, info.vi_MsgsTx, info.vi_MsgsRx, info.vi_SlotsIn, info.vi_SlotsOut, info.vi_BellRings,
	    info.vi_Interrupts, info.vi_BulksDone, info.vi_Unexpected, info.vi_Dropped, info.vi_RxStalls);
	Printf((CONST_STRPTR) "reach: 0x%08lx-0x%08lx at bus 0x%08lx; uncached 0x%08lx-0x%08lx\n", info.vi_ReachStart, info.vi_ReachEnd,
	    info.vi_ReachBus, info.vi_UncachedStart, info.vi_UncachedEnd);
	return RETURN_OK;
}

/* ---- GCMD -------------------------------------------------------------------- */

/**
 * text_after() - What follows a mark in a text.
 * @text: the text
 * @mark: what comes before
 *
 * Return: the first character after the mark, NULL if the text does not have it.
 */
static char *text_after(char *text, const char *mark)
{
	char *at = strstr(text, mark);
	return at != NULL ? at + strlen(mark) : NULL;
}

/**
 * number_after() - The number that follows a mark in a text.
 * @text: the text
 * @mark: what comes before the number, with the answer's name: an answer of
 *        "error=1 error_msg=..." must not read as a value
 * @n:    where the number goes
 *
 * Return: TRUE if the mark is followed by a number, decimal or "0x" hex.
 */
static BOOL number_after(char *text, const char *mark, ULONG *n)
{
	char *digits = text_after(text, mark);
	if (digits == NULL)
		return FALSE;
	char *end;
	*n = strtoul(digits, &end, 0);
	return end != digits;
}

/**
 * cut_at() - End a text at the first of some characters.
 * @text: the text, cut in place
 * @ends: the characters
 *
 * Return: @text.
 */
static char *cut_at(char *text, const char *ends)
{
	text[strcspn(text, ends)] = 0;
	return text;
}

/**
 * trim_end() - Cut the spaces and newlines off a text's end.
 * @text: the text, cut in place
 */
static void trim_end(char *text)
{
	ULONG n = (ULONG)strlen(text);
	while (n > 0 && (text[n - 1u] == '\n' || text[n - 1u] == ' '))
		n--;
	text[n] = 0;
}

/**
 * gcmd_ask() - One gencmd command on the open GCMD service, and its answer.
 * @cmd:    the command
 * @status: where gencmd's status goes (0: done)
 * @text:   where the answer goes, NUL-terminated
 * @room:   its bytes
 * @took:   where the round trip's microseconds go, or NULL
 * @bytes:  where the reply's length goes, or NULL
 *
 * Return: TRUE if an answer came; FALSE on a write error (printed) or no reply.
 */
static BOOL gcmd_ask(const char *cmd, LONG *status, char *text, ULONG room, u32 *took, ULONG *bytes)
{
	u32 t0 = get_time();
	LONG q = vq_do(&svc, CMD_WRITE, cmd, (u32)strlen(cmd) + 1u, NULL);
	if (q != 0)
	{
		Printf((CONST_STRPTR) "  write \"%s\": %ld\n", cmd, q);
		return FALSE;
	}
	UBYTE *data;
	ULONG len;
	if (!vq_next(&svc, &data, &len, VQ_REPLY_TIMEOUT_MS))
		return FALSE;
	if (took != NULL)
		*took = get_time() - t0;
	if (bytes != NULL)
		*bytes = len;
	*status = len >= 4 ? (LONG)VCHIQ_LE32(*(ULONG *)data) : -1;
	ULONG n = len > 4 ? umin(len - 4u, room - 1u) : 0;
	CopyMem(data + 4, text, n);
	text[n] = 0;
	trim_end(text);
	vq_consume(&svc);
	return TRUE;
}

/**
 * do_gcmd() - gencmd commands through the GCMD service, each answer printed.
 * @cmds:  the commands, each sent in turn
 * @ncmds: how many
 *
 * A command left unanswered dumps the shared window, at once and 2 s later.
 *
 * Return: RETURN_OK, RETURN_FAIL if the service did not open, a command went
 * unanswered or one answered with an error.
 */
static int do_gcmd(const char *const *cmds, ULONG ncmds)
{
	if (!vq_open(&svc, &timer, GCMD_FOURCC, 1, 1, "GCMD"))
		return RETURN_FAIL;

	int rc = RETURN_OK;
	for (ULONG c = 0; c < ncmds; c++)
	{
		LONG status;
		char text[256];
		u32 took = 0;
		ULONG bytes = 0;
		if (!gcmd_ask(cmds[c], &status, text, sizeof(text), &took, &bytes))
		{
			Printf((CONST_STRPTR) "  \"%s\": no reply\n", cmds[c]);
			struct VchiqInfo info;
			if (vq_get_info(&svc, &info))
			{
				dump_window(&info, "at the timeout");
				Delay(100); /* 2 s: does anything still move? */
				if (vq_get_info(&svc, &info))
					dump_window(&info, "2 s later");
			}
			rc = RETURN_FAIL;
			break;
		}
		Printf((CONST_STRPTR) "  \"%s\" -> status %ld, %ld bytes, %ld us: %s\n", cmds[c], status, bytes, (ULONG)took, text);
		if (status != 0)
			rc = RETURN_FAIL;
	}

	vq_close(&svc);
	Printf((CONST_STRPTR) "close GCMD\n");
	return rc;
}

/* ---- the summary --------------------------------------------------------------- */

/* The summary's state: the worst outcome so far, and the warnings. */
struct summary
{
	int rc;         /* RETURN_OK, RETURN_WARN, RETURN_ERROR (not installed) or RETURN_FAIL */
	ULONG warnings;
};

/**
 * worse() - Note an outcome; the summary keeps the worst.
 * @sum: the summary
 * @rc:  RETURN_WARN, RETURN_ERROR or RETURN_FAIL
 */
static void worse(struct summary *sum, int rc)
{
	if (rc == RETURN_WARN)
		sum->warnings++;
	if (rc > sum->rc)
		sum->rc = rc;
}

/* get_throttled's bits and their meanings, as the vcgencmd documentation words them */
static const struct
{
	ULONG mask;
	const char *meaning;
} throttle_bits[] = {
    {0x1, "Undervoltage detected"},
    {0x2, "Arm frequency capped"},
    {0x4, "Currently throttled"},
    {0x8, "Soft temperature limit active"},
    {0x10000, "Undervoltage has occurred"},
    {0x20000, "Arm frequency capping has occurred"},
    {0x40000, "Throttling has occurred"},
    {0x80000, "Soft temperature limit has occurred"},
};

/**
 * print_throttle() - Print what get_throttled's word says, or "none".
 * @value: the word
 */
static void print_throttle(ULONG value)
{
	const char *sep = "";
	for (ULONG i = 0; i < ARRAY_SIZE(throttle_bits); i++)
		if (value & throttle_bits[i].mask)
		{
			Printf((CONST_STRPTR) "%s%s", sep, throttle_bits[i].meaning);
			sep = ", ";
		}
	if (*sep == 0)
		Printf((CONST_STRPTR) "none");
}

/**
 * summary_emu68() - The parts Emu68 provides, read without vchiq.device: the resources and the VCHIQ node.
 * @sum: the summary
 *
 * Return: TRUE if vchiq.device can start.
 */
static BOOL summary_emu68(struct summary *sum)
{
	DeviceTreeBase = OpenResource((CONST_STRPTR) "devicetree.resource");
	APTR mailbox = OpenResource((CONST_STRPTR) "mailbox.resource");
	if (DeviceTreeBase == NULL || mailbox == NULL)
	{
		Printf((CONST_STRPTR) "  Emu68:          devicetree.resource or mailbox.resource missing\n");
		Printf((CONST_STRPTR) "  FAIL: not running under Emu68 1.1 or later\n");
		worse(sum, RETURN_FAIL);
		return FALSE;
	}
	Printf((CONST_STRPTR) "  Emu68:          device tree and mailbox present\n");

	APTR node = DT_OpenSymbol(DeviceTreeBase, (CONST_STRPTR) "vchiq"); /* no alias names it */
	if (node == NULL)
	{
		Printf((CONST_STRPTR) "  VCHIQ node:     none (no \"vchiq\" label in the Pi's device tree)\n");
		Printf((CONST_STRPTR) "  FAIL: this Emu68 or Pi firmware gives no VCHIQ node\n");
		worse(sum, RETURN_FAIL);
		return FALSE;
	}
	CONST_STRPTR compatible = DT_GetPropValue(DT_FindProperty(node, (CONST_STRPTR) "compatible"));
	Printf((CONST_STRPTR) "  VCHIQ node:     %s (%s)\n", (const char *)DT_GetKeyName(node),
	    compatible != NULL ? (const char *)compatible : "?");
	DT_CloseKey(node);

	if (OpenResource((CONST_STRPTR) "cma.resource") == NULL)
	{
		Printf((CONST_STRPTR) "  CMA:            cma.resource missing\n");
		Printf((CONST_STRPTR) "  FAIL: add dtoverlay=cma to the Pi's config.txt, then press Ctrl-Amiga-Amiga\n");
		worse(sum, RETURN_FAIL);
		return FALSE;
	}
	Printf((CONST_STRPTR) "  CMA:            cma.resource present\n");
	return TRUE;
}

/**
 * summary_device() - vchiq.device: whether it opens, and how its connection came up at this boot.
 * @sum: the summary
 *
 * Return: TRUE if the connection is up.
 */
static BOOL summary_device(struct summary *sum)
{
	if (!vq_open(&svc, &timer, 0, 0, 0, NULL))
	{
		if (svc.error == IOERR_OPENFAIL)
		{
			Printf((CONST_STRPTR) "  vchiq.device:   did not load\n");
			Printf((CONST_STRPTR) "  FAIL: copy vchiq.device to DEVS: (the Install script's vchiq.device)\n");
			worse(sum, RETURN_ERROR);
		}
		else
		{
			Printf((CONST_STRPTR) "  vchiq.device:   no link to the VideoCore (error %ld)\n", svc.error);
			Printf((CONST_STRPTR) "  FAIL: press Ctrl-Amiga-Amiga, and if that does not help power off and on; README, Troubleshooting\n");
			worse(sum, RETURN_FAIL);
		}
		return FALSE;
	}
	struct Library *device = &svc.io->iovq_Req.io_Device->dd_Library;
	struct VchiqInfo info;
	BOOL ok = vq_get_info(&svc, &info);
	Printf((CONST_STRPTR) "  vchiq.device:   %ld.%ld, connected", (ULONG)device->lib_Version, (ULONG)device->lib_Revision);
	vq_close(&svc);
	if (ok)
	{
		static const char *const bringups[] = {"a fresh start", "the previous session resumed", "the previous session repaired",
		                                       "started afresh: the VideoCore had restarted"};
		Printf((CONST_STRPTR) ", session %ld (%s)", info.vi_Sessions,
		    info.vi_Bringup < ARRAY_SIZE(bringups) ? bringups[info.vi_Bringup] : "?");
	}
	Printf((CONST_STRPTR) "\n");
	return TRUE;
}

/**
 * summary_firmware() - The VideoCore firmware through gencmd: version, temperature, throttling, clocks, gpu_mem.
 * @sum: the summary
 */
static void summary_firmware(struct summary *sum)
{
	if (!vq_open(&svc, &timer, GCMD_FOURCC, 1, 1, NULL))
	{
		Printf((CONST_STRPTR) "  firmware:       the gencmd service did not open (error %ld)\n", svc.error);
		Printf((CONST_STRPTR) "  FAIL: press Ctrl-Amiga-Amiga, and if that does not help power off and on; README, Troubleshooting\n");
		worse(sum, RETURN_FAIL);
		return;
	}
	LONG status;
	char text[256];
	if (!gcmd_ask("version", &status, text, sizeof(text), NULL, NULL) || status != 0)
	{
		Printf((CONST_STRPTR) "  firmware:       no answer from gencmd\n");
		Printf((CONST_STRPTR) "  FAIL: press Ctrl-Amiga-Amiga, and if that does not help power off and on; README, Troubleshooting\n");
		worse(sum, RETURN_FAIL);
		vq_close(&svc);
		return;
	}
	/* "<date>\nCopyright ...\nversion <hash> (...)": the date and the hash */
	char *hash = text_after(text, "\nversion ");
	if (hash != NULL)
		cut_at(hash, " \n");
	trim_end(cut_at(text, "\n"));
	Printf((CONST_STRPTR) "  firmware:       %s%s%s\n", text, hash != NULL ? ", version " : "", hash != NULL ? hash : "");

	ULONG value;
	if (gcmd_ask("measure_temp", &status, text, sizeof(text), NULL, NULL) && status == 0)
	{
		/* "temp=48.2'C" */
		char *temperature = text_after(text, "temp=");
		if (temperature != NULL)
			Printf((CONST_STRPTR) "  temperature:    %s C\n", cut_at(temperature, "'"));
	}

	if (gcmd_ask("get_throttled", &status, text, sizeof(text), NULL, NULL) && status == 0 && number_after(text, "throttled=", &value))
	{
		Printf((CONST_STRPTR) "  throttling:     ");
		print_throttle(value);
		Printf((CONST_STRPTR) "\n");
		ULONG seen = (value | (value >> 16)) & 0xfu; /* now or since the Pi started */
		if (seen & 1u)
		{
			Printf((CONST_STRPTR) "  WARN: the Pi's supply voltage drops: use a stronger power supply\n");
			worse(sum, RETURN_WARN);
		}
		if (seen & 8u)
		{
			Printf((CONST_STRPTR) "  WARN: the Pi reached its soft temperature limit: give it better cooling\n");
			worse(sum, RETURN_WARN);
		}
		if ((seen & 6u) != 0 && (seen & 9u) == 0) /* capped or throttled, and the word gives no cause */
		{
			Printf((CONST_STRPTR) "  WARN: the Pi's clocks were held back: check its cooling and power supply\n");
			worse(sum, RETURN_WARN);
		}
	}

	ULONG arm = 0, core = 0;
	/* "frequency(48)=1500345728" */
	if (gcmd_ask("measure_clock arm", &status, text, sizeof(text), NULL, NULL) && status == 0)
		(void)number_after(text, ")=", &arm);
	if (gcmd_ask("measure_clock core", &status, text, sizeof(text), NULL, NULL) && status == 0)
		(void)number_after(text, ")=", &core);
	if (arm != 0 || core != 0)
		Printf((CONST_STRPTR) "  clocks:         ARM %ld MHz, core %ld MHz\n", (arm + 500000u) / 1000000u,
		    (core + 500000u) / 1000000u);

	if (gcmd_ask("get_mem gpu", &status, text, sizeof(text), NULL, NULL) && status == 0 && number_after(text, "gpu=", &value))
		Printf((CONST_STRPTR) "  gpu_mem:        %ld MiB\n", value);
	vq_close(&svc);
}

/**
 * do_summary() - The setup in plain words: each part OK, or a warning or failure with its cure.
 *
 * Return: RETURN_OK, RETURN_WARN, RETURN_ERROR (a part not installed) or RETURN_FAIL.
 */
static int do_summary(void)
{
	struct summary sum = {RETURN_OK, 0};
	Printf((CONST_STRPTR) "VideoCore setup\n");
	if (summary_emu68(&sum) && summary_device(&sum))
		summary_firmware(&sum);
	if (sum.rc == RETURN_OK)
		Printf((CONST_STRPTR) "PASS\n");
	else if (sum.rc == RETURN_WARN)
		Printf((CONST_STRPTR) "PASS, %ld warning%s\n", sum.warnings, sum.warnings == 1 ? "" : "s");
	else
		Printf((CONST_STRPTR) "FAIL\n");
	return sum.rc;
}

/* ---- main -------------------------------------------------------------------- */

enum
{
	ARG_GCMD,
	ARG_COUNTERS,
	ARG_DUMP,
	ARG_COUNT
};

/**
 * run_command() - GCMD, COUNTERS or DUMP, with its verdict.
 * @args: ReadArgs()'s results, one command given
 *
 * Return: the command's return code.
 */
static int run_command(const LONG *args)
{
	int rc;
	if (args[ARG_GCMD] != 0)
	{
		const char *cmd = (const char *)args[ARG_GCMD];
		rc = do_gcmd(&cmd, 1);
	}
	else if (args[ARG_COUNTERS] != 0)
		rc = do_counters();
	else
		rc = do_dump();
	Printf((CONST_STRPTR) "%s\n", rc == RETURN_OK ? "PASS" : "FAIL");
	return rc;
}

/**
 * main() - Entry point: with no argument the summary, else one of GCMD, COUNTERS or DUMP.
 *
 * More than one of them is refused.
 *
 * Return: RETURN_OK for a pass, RETURN_WARN, RETURN_ERROR or RETURN_FAIL otherwise.
 */
int main(void)
{
	LONG args[ARG_COUNT] = {0};
	struct RDArgs *rda = ReadArgs((CONST_STRPTR) "GCMD/K,COUNTERS/S,DUMP/S", args, NULL);
	if (rda == NULL)
	{
		PrintFault(IoErr(), (CONST_STRPTR) "VCHIQInfo");
		return RETURN_FAIL;
	}

	ULONG commands = 0;
	for (ULONG i = 0; i < ARG_COUNT; i++)
		if (args[i] != 0)
			commands++;
	if (commands > 1)
	{
		Printf((CONST_STRPTR) "one of GCMD, COUNTERS or DUMP at a time\n");
		FreeArgs(rda);
		return RETURN_FAIL;
	}
	if (!drv_timer_open(&timer, SysBase))
	{
		Printf((CONST_STRPTR) "no timer\n");
		FreeArgs(rda);
		return RETURN_FAIL;
	}

	int rc = commands == 0 ? do_summary() : run_command(args);

	drv_timer_close(&timer);
	FreeArgs(rda);
	return rc;
}
