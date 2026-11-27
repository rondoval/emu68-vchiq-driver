/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * device.c - resident, device init/open/close/expunge.
 *
 * Init only checks the cache-ops flavour and installs the reset guard, as the
 * stack's other devices do.  The hardware is looked up (devtree_parse.c) and
 * the connection to the VPU brought up at the first OpenDevice(), kept across
 * closes and taken down at expunge (vchiq_link.c).  Each open is a unit with one VCHIQ service
 * (unit.c), opened and closed in the caller, waiting for the VPU's answer.
 */
#include <exec/memory.h>
#include <exec/resident.h>

#include <emu68_features.h>
#include <minlist.h>

#include <device.h>

LONG __attribute__((used, section(".text.entry"))) doNotExecute(void);
/**
 * doNotExecute() - Entry point of the file, so that running it as a program does nothing.
 *
 * Placed first in .text.entry.
 *
 * Return: -1.
 */
LONG __attribute__((used, section(".text.entry"))) doNotExecute(void)
{
	return -1;
}

extern const UBYTE endOfCode;
static const char deviceName[] = DEVICE_NAME;
static const char deviceIdString[] = DEVICE_IDSTRING;
static const APTR initTable[4];

static const struct Resident vchiqDeviceResident __attribute__((used, section(".text.modhdr"))) = {
    RTC_MATCHWORD,
    (struct Resident *)&vchiqDeviceResident,
    (APTR)&endOfCode,
    RTF_AUTOINIT | RTF_COLDSTART,
    DEVICE_VERSION,
    NT_DEVICE,
    DEVICE_PRIORITY,
    (APTR)&deviceName,
    (APTR)&deviceIdString,
    (APTR)initTable,
};

/* ---- device vectors ------------------------------------------------------------ */

/**
 * expungeLib() - Device expunge vector.
 * @base: the device base
 *
 * Deferred (LIBF_DELEXP) while open. The ColdReboot vector may have been re-patched on top of our
 * stub: then the code must stay resident. Otherwise takes the connection down and frees the base.
 *
 * Return: the segment list to unload, or 0 if the device stays.
 */
static ULONG expungeLib(struct VCHIQDevice *base asm("a6"))
{
	struct ExecBase *SysBase = base->sysBase;
	ULONG segList = base->segList;

	if (base->device.dd_Library.lib_OpenCnt > 0)
	{
		base->device.dd_Library.lib_Flags |= LIBF_DELEXP;
		return 0;
	}

	/* The ColdReboot vector may have been re-patched on top of our stub: then the code must stay resident. */
	if (!reset_guard_remove(&base->resetGuard))
		return 0;
	vchiq_link_down(base);
	drv_waitq_cleanup(&base->waitq, SysBase);

	Forbid();
	Remove((struct Node *)base);
	Permit();

	ULONG size = (ULONG)base->device.dd_Library.lib_NegSize + base->device.dd_Library.lib_PosSize;
	FreeMem((APTR)((ULONG)base - base->device.dd_Library.lib_NegSize), size);
	return segList;
}

/**
 * vchiq_reset_keyboard() - The reset guard's keyboard path: Ctrl-Amiga-Amiga.
 * @user: the struct VCHIQDevice
 *
 * Interrupt context.  Ctrl-Amiga-Amiga restarts the whole Pi, the VPU with it, so
 * there is no connection to leave in order for a next session: report done at once
 * instead of spending up to 1.65 s closing a link that is about to vanish.  The
 * ColdReboot path restarts the 68k alone and still quiesces (vchiq_link_reset()).
 */
static void vchiq_reset_keyboard(APTR user)
{
	struct VCHIQDevice *base = user;
	reset_guard_done(&base->resetGuard);
}

/**
 * initFunction() - Device init (RTF_AUTOINIT): check the cache-ops flavour and install the reset
 *   guard.
 * @base: the new device base
 * @segList: the device's segment list
 * @SysBase: exec
 *
 * Refuses to load (and frees the base) if Emu68 lacks dcache-range-ops. The hardware is looked up at
 * the first open, not here.
 *
 * Return: the base, or NULL if the device does not load.
 */
static APTR initFunction(struct VCHIQDevice *base asm("d0"), ULONG segList asm("a0"), struct ExecBase *SysBase asm("a6"))
{
	if (!emu68_has_dcache_range_ops(SysBase))
	{
		Kprintf("[vchiq] %s: rangeops build, but Emu68 lacks dcache-range-ops rev 1 - refusing to load. Install the standard driver package or update Emu68.\n", __func__);
		ULONG size = (ULONG)base->device.dd_Library.lib_NegSize + base->device.dd_Library.lib_PosSize;
		FreeMem((APTR)((ULONG)base - base->device.dd_Library.lib_NegSize), size);
		return NULL;
	}

	base->segList = segList;
	base->sysBase = SysBase;
	base->device.dd_Library.lib_Revision = (UWORD)DEVICE_REVISION;
	InitSemaphore(&base->lock);
	drv_waitq_init(&base->waitq);
	_NewMinList(&base->units);
	base->irq_signal = -1;

	if (reset_guard_install(&base->resetGuard, SysBase, vchiq_link_reset, base, (CONST_STRPTR)deviceName))
		reset_guard_set_defer(&base->resetGuard, vchiq_reset_keyboard);
	else
		Kprintf("[vchiq] %s: reset guard install failed\n", __func__);

	return base;
}

/**
 * openLib() - Device open vector: unit 0, a unit with one VCHIQ service.
 * @io: a struct IOVchiqReq naming the service
 * @unitNumber: must be 0
 * @flags: unused
 * @base: the device base
 *
 * Brings the connection up on first use, then opens the service and waits for the VPU's answer (the
 * lock is released meanwhile, Exec's Forbid() broken by the wait, as by any device that talks to its
 * hardware here).
 */
static void openLib(struct IOVchiqReq *io asm("a1"), ULONG unitNumber asm("d0"), ULONG flags asm("d1"), struct VCHIQDevice *base asm("a6"))
{
	struct ExecBase *SysBase = base->sysBase;
	(void)flags;

	if (unitNumber != 0 || io->iovq_Req.io_Message.mn_Length < sizeof(struct IOVchiqReq))
	{
		io->iovq_Req.io_Error = IOERR_OPENFAIL;
		io->iovq_Req.io_Device = NULL; /* a CloseDevice() after the failed open must not reach closeLib */
		io->iovq_Req.io_Unit = NULL;
		return;
	}

	base->device.dd_Library.lib_OpenCnt++; /* no expunge while we wait */
	ObtainSemaphore(&base->lock);
	s32 rc = base->up ? 0 : vchiq_link_up(base);
	struct VCHIQUnit *unit = rc == 0 ? UnitOpen(base, io, &rc) : NULL;
	if (unit != NULL)
	{
		io->iovq_Req.io_Unit = &unit->unit;
		io->iovq_Req.io_Error = 0;
		io->iovq_PeerVersion = unit->svc != NULL ? (UWORD)unit->svc->peer_version : 0;
		io->iovq_Req.io_Message.mn_Node.ln_Type = NT_REPLYMSG; /* CheckIO() of the unused request says done */
		base->device.dd_Library.lib_Flags &= (UBYTE)~LIBF_DELEXP;
	}
	else
	{
		io->iovq_Req.io_Error = (BYTE)rc;
		io->iovq_Req.io_Device = NULL; /* a CloseDevice() after the failed open must not reach closeLib */
		io->iovq_Req.io_Unit = NULL;
		base->device.dd_Library.lib_OpenCnt--;
	}
	ReleaseSemaphore(&base->lock);
}

/**
 * closeLib() - Device close vector: close the opener's unit.
 * @io: the request that opened it
 * @base: the device base
 *
 * Return: the segment list if the close completed a deferred expunge, else 0.
 */
static ULONG closeLib(struct IOVchiqReq *io asm("a1"), struct VCHIQDevice *base asm("a6"))
{
	struct ExecBase *SysBase = base->sysBase;
	struct VCHIQUnit *unit = (struct VCHIQUnit *)io->iovq_Req.io_Unit;

	ObtainSemaphore(&base->lock);
	if (unit != NULL)
		UnitClose(base, unit);
	io->iovq_Req.io_Unit = NULL;
	io->iovq_Req.io_Device = NULL;
	ReleaseSemaphore(&base->lock);

	base->device.dd_Library.lib_OpenCnt--;
	if (base->device.dd_Library.lib_OpenCnt == 0 && (base->device.dd_Library.lib_Flags & LIBF_DELEXP))
		return expungeLib(base);
	return 0;
}

/**
 * extFunc() - Reserved device vector.
 * @base: the device base, unused
 *
 * Return: NULL.
 */
static APTR extFunc(struct VCHIQDevice *base asm("a6"))
{
	(void)base;
	return NULL;
}

static const APTR funcTable[] = {
    (APTR)openLib,
    (APTR)closeLib,
    (APTR)expungeLib,
    (APTR)extFunc,
    (APTR)beginIO,
    (APTR)abortIO,
    (APTR)-1,
};

static const APTR initTable[4] = {
    (APTR)sizeof(struct VCHIQDevice),
    (APTR)funcTable,
    NULL,
    (APTR)initFunction,
};
