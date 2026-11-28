/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * device.c - DEVICE: the commands vchiq.device answers itself, on an open
 * GCMD service.  NSCMD_DEVICEQUERY's list of commands (and its refusal of a
 * result too short to fill), then CMD_FLUSH with the service's reads posted:
 * every read must come back IOERR_ABORTED, and the service must still answer
 * gencmd once they are posted again.
 */
#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#include <clib/dos_protos.h>
#else
#include <proto/exec.h>
#include <proto/dos.h>
#endif

#include <dos/dos.h>
#include <exec/errors.h>
#include <devices/newstyle.h>

#include <string.h>

#include <emu68test/kv.h>
#include <vchiq/vchiq_test.h>

#include "commands.h"

const char device_template[] = "";

#define FLUSH_WAIT_TICKS 5u  /* a read given back by CMD_FLUSH: looked for this often (1/50 s) */
#define FLUSH_WAIT_TRIES 50u /* and this many times, a second, before it counts as not given back */
#define SHORT_RESULT 4u      /* a query result too short to fill: the device refuses it */
#define MAX_COMMANDS 32u     /* the most commands of the list printed */

/* A command the device may list, and its name */
struct command_name
{
	UWORD command;
	const char *name;
};

static const struct command_name command_names[] = {
    {CMD_READ, "CMD_READ"},
    {CMD_WRITE, "CMD_WRITE"},
    {CMD_FLUSH, "CMD_FLUSH"},
    {NSCMD_DEVICEQUERY, "NSCMD_DEVICEQUERY"},
    {NSCMD_VQ_BULKWRITE, "NSCMD_VQ_BULKWRITE"},
    {NSCMD_VQ_BULKREAD, "NSCMD_VQ_BULKREAD"},
    {NSCMD_VQ_GETINFO, "NSCMD_VQ_GETINFO"},
    {NSCMD_VQ_TEST_ABANDON, "NSCMD_VQ_TEST_ABANDON"},
};

/**
 * command_name() - A command's name.
 * @command: the command's number
 *
 * Return: its name, or "?" for one this table does not know.
 */
static const char *command_name(UWORD command)
{
	for (ULONG i = 0; i < sizeof(command_names) / sizeof(command_names[0]); i++)
		if (command_names[i].command == command)
			return command_names[i].name;
	return "?";
}

/**
 * device_query() - NSCMD_DEVICEQUERY: the refusal of a short result, then the list of commands.
 * @s: the service, open
 *
 * Prints devicequery.short_error, devicequery.error, devicequery.type and
 * devicequery.command.N.name and .number for each command listed.
 *
 * Return: TRUE when the query was answered.
 */
static BOOL device_query(struct vq_service *s)
{
	struct NSDeviceQueryResult result;
	memset(&result, 0, sizeof(result));
	kv_num("devicequery.short_error", vq_do(s, NSCMD_DEVICEQUERY, &result, SHORT_RESULT, NULL));
	ULONG actual = 0;
	LONG error = vq_do(s, NSCMD_DEVICEQUERY, &result, sizeof(result), &actual);
	kv_num("devicequery.error", error);
	if (error != 0)
		return FALSE;
	kv_unum("devicequery.actual", actual);
	kv_unum("devicequery.size", result.nsdqr_SizeAvailable);
	kv_unum("devicequery.type", result.nsdqr_DeviceType);
	const UWORD *commands = result.nsdqr_SupportedCommands;
	ULONG n = 0;
	for (; commands != NULL && commands[n] != 0 && n < MAX_COMMANDS; n++)
	{
		kv_str(kv_at("devicequery.command", n, "name"), command_name(commands[n]));
		kv_unum(kv_at("devicequery.command", n, "number"), commands[n]);
	}
	kv_unum("devicequery.commands", n);
	return TRUE;
}

/**
 * read_given_back() - Wait a second at most for a read the device should give back now.
 * @read: the read, posted
 *
 * A read still out after the wait is aborted and collected, so the service
 * can close.
 *
 * Return: the read's io_Error, or -1 when it was not given back by itself.
 */
static LONG read_given_back(struct IOVchiqReq *read)
{
	for (ULONG tries = 0; tries < FLUSH_WAIT_TRIES; tries++)
	{
		if (CheckIO((struct IORequest *)read))
		{
			WaitIO((struct IORequest *)read);
			return read->iovq_Req.io_Error;
		}
		Delay(FLUSH_WAIT_TICKS);
	}
	AbortIO((struct IORequest *)read);
	WaitIO((struct IORequest *)read);
	return -1;
}

/**
 * device_flush() - CMD_FLUSH with the service's reads posted: each comes back IOERR_ABORTED.
 * @s: the service, open, its reads posted
 *
 * Prints flush.error, flush.reads (the reads that were out), flush.aborted
 * (those given back with IOERR_ABORTED) and flush.read.N.error; the reads are
 * posted again afterwards.
 *
 * Return: TRUE when CMD_FLUSH was answered.
 */
static BOOL device_flush(struct vq_service *s)
{
	LONG error = vq_do(s, CMD_FLUSH, NULL, 0, NULL);
	kv_num("flush.error", error);
	ULONG aborted = 0;
	for (ULONG k = 0; k < VQ_READS; k++)
	{
		LONG read_error = read_given_back(s->rd[k]);
		kv_num(kv_at("flush.read", k, "error"), read_error);
		aborted += read_error == IOERR_ABORTED ? 1u : 0u;
	}
	kv_unum("flush.reads", VQ_READS);
	kv_unum("flush.aborted", aborted);
	vq_repost(s);
	return error == 0;
}

/**
 * device_answers() - The service after the flush: gencmd's "version" sent, its answer read.
 * @s: the service, open, its reads posted again
 *
 * Return: TRUE when the answer came.
 */
static BOOL device_answers(struct vq_service *s)
{
	static const char command[] = "version";
	UBYTE *data;
	ULONG length = 0;
	BOOL answered = vq_do(s, CMD_WRITE, command, sizeof(command), NULL) == 0 &&
	                vq_next(s, &data, &length, VQ_REPLY_TIMEOUT_MS);
	if (answered)
		vq_consume(s);
	kv_bool("flush.answers_after", answered);
	kv_unum("flush.answer_bytes", length);
	return answered;
}

/**
 * do_device() - DEVICE: NSCMD_DEVICEQUERY and CMD_FLUSH on a GCMD service.
 * @t:    the tool
 * @args: device_template's (none)
 *
 * Return: RETURN_OK; RETURN_FAIL when the service did not open, or the device refused a command or stopped answering.
 */
int do_device(struct tool *t, LONG *args)
{
	(void)args;
	struct vq_service *s = &t->svc;
	if (!vq_open(s, &t->waiter.timer, GCMD_FOURCC, 1, 1, NULL))
	{
		kv_num("vchiq.error", s->error);
		kv_str("fail", "the GCMD service did not open");
		return RETURN_FAIL;
	}
	BOOL answered = device_query(s) && device_flush(s) && device_answers(s);
	vq_close(s);
	if (!answered)
		kv_str("fail", "the device refused a command or stopped answering");
	return answered ? RETURN_OK : RETURN_FAIL;
}
