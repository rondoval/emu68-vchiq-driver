/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * VCHIQTest - the test rig of vchiq.device and videocore.audio, driven by the
 * test suite (tests/, developer-guide.md section 6).  One command per run; its
 * facts come out as key=value lines (kv.h), the verdict last.  VCHIQInfo shows
 * the same state for people.
 *
 *   VCHIQTest ?                        the commands
 *   VCHIQTest <command> ?              a command's template
 *
 *   STATUS                             vchiq.device's connection and counters
 *   MARK text                          a mark in the debug log (the capture self-test)
 *   ABANDON                            leave the connection as a crash would (the repair test;
 *                                      needs a vchiq.device built with VCHIQ_TEST_HOOKS)
 *   AUDIO [MS n] [DEST n] [RATE n] [CHANNELS n] [CHUNK ms] [DEPTH n] [TONE] [WALK] [VOL n] [RESTART]
 *                                      the firmware audio service driven directly
 *   AHIWALK MODE n [RATE n]            the channel walk through an AHI mode
 *   AHITONE MODE n [MS n] [RATE n] [HZ n]
 *                                      the tone through an AHI mode, start and stop timed
 *   DEVICE                             vchiq.device's own commands: NSCMD_DEVICEQUERY, CMD_FLUSH
 *
 * Return codes: 0 the command did its work and its checks passed (PASS),
 * 5 a check failed, 10 the environment (a library, device, file or memory
 * missing, no test hooks), 20 the API refused or failed, or the arguments
 * made no sense (then before the format line).
 *
 * Hosted CLI program (libnix).  A VCHIQ service is plain Exec I/O
 * (vchiq_client.c).
 */
#ifdef __INTELLISENSE__
#include <clib/exec_protos.h>
#include <clib/dos_protos.h>
#else
#include <proto/exec.h>
#include <proto/dos.h>
#endif

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <string.h>

#include <debugport.h>
#include <emu68test/kv.h>
#include <strutil.h>

#include "commands.h"

/* libnix swaps to a stack of this size: the library's calls and waits run on it */
unsigned long __stack = 32768;

static const char verstag[] __attribute__((used)) = VERSTAG;

/* A command: its name, its ReadArgs() template, its entry point */
struct command
{
	const char *name;     /* what the command line calls it, any case */
	const char *template; /* its ReadArgs() template */
	command_fn run;       /* its entry point */
};

static int do_mark(struct tool *t, LONG *args);

static const struct command commands[] = {
    {"STATUS", status_template, do_status},     {"MARK", "TEXT/F", do_mark},
    {"ABANDON", abandon_template, do_abandon},  {"AUDIO", audio_template, do_audio},
    {"AHIWALK", ahiwalk_template, do_ahiwalk},  {"AHITONE", ahitone_template, do_ahitone},
    {"DEVICE", device_template, do_device},
};

/**
 * do_mark() - MARK: a line in the debug log, and the same text as mark=.
 * @t:    the tool
 * @args: TEXT/F
 *
 * Return: RETURN_OK.
 */
static int do_mark(struct tool *t, LONG *args)
{
	(void)t;
	const char *text = args[0] != 0 ? (const char *)args[0] : "";
	kv_str("mark", text);
	debugport_printf("[VCHIQTest] mark %s\n", text);
	return RETURN_OK;
}

/**
 * command_named() - The command a name picks, any case.
 * @name: the name
 *
 * Return: the command, NULL for none.
 */
static const struct command *command_named(const char *name)
{
	for (ULONG i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
		if (_Stricmp((CONST_STRPTR)name, (CONST_STRPTR)commands[i].name) == 0)
			return &commands[i];
	return NULL;
}

/**
 * parse_rest() - Read a command's arguments from the rest of the line.
 * @template: the command's template
 * @rest:     the line after the command's name, or NULL
 * @args:     the array to fill, zeroed, ARGS_MAX entries
 *
 * A template of more entries than @args holds is refused: ReadArgs() would
 * write past it.
 *
 * Return: the RDArgs to free with FreeArgs() and FreeDosObject(); NULL on a usage error (IoErr() says).
 */
static struct RDArgs *parse_rest(const char *template, const char *rest, LONG *args)
{
	ULONG entries = 1;
	for (const char *p = template; *p != 0; p++)
		entries += *p == ',';
	if (entries > ARGS_MAX)
	{
		SetIoErr(ERROR_LINE_TOO_LONG); /* ReadArgs() would write past args[] */
		return NULL;
	}

	static char line[512];
	ULONG n = 0;
	for (const char *p = rest != NULL ? rest : ""; *p != 0 && n < sizeof(line) - 2u; p++)
		line[n++] = *p;
	line[n++] = '\n';
	line[n] = 0;
	struct RDArgs *rda = AllocDosObject(DOS_RDARGS, NULL);
	if (rda == NULL)
		return NULL;
	rda->RDA_Source.CS_Buffer = (STRPTR)line;
	rda->RDA_Source.CS_Length = (LONG)n;
	rda->RDA_Source.CS_CurChr = 0;
	rda->RDA_Flags |= RDAF_NOPROMPT;
	if (ReadArgs((CONST_STRPTR)template, args, rda) == NULL)
	{
		FreeDosObject(DOS_RDARGS, rda);
		return NULL;
	}
	return rda;
}

/**
 * main() - Entry point: look up the command, parse its arguments and run it with a kv grammar around it.
 *
 * VCHIQTest ? lists the commands, VCHIQTest COMMAND ? one command's template.
 *
 * Return: the command's return code via kv_end(), RETURN_OK for the help, RETURN_FAIL for a bad command or arguments.
 */
int main(void)
{
	LONG top[2] = {0, 0};
	struct RDArgs *rda = ReadArgs((CONST_STRPTR) "COMMAND/A,ARGS/F", top, NULL);
	if (rda == NULL)
	{
		PrintFault(IoErr(), (CONST_STRPTR) "VCHIQTest");
		return RETURN_FAIL;
	}
	const char *name = (const char *)top[0], *rest = (const char *)top[1];
	int rc = RETURN_FAIL;
	if (strcmp(name, "?") == 0 || _Stricmp((CONST_STRPTR)name, (CONST_STRPTR) "HELP") == 0)
	{
		for (ULONG i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
			Printf((CONST_STRPTR) "%s %s\n", commands[i].name, commands[i].template);
		rc = RETURN_OK;
		goto out_top;
	}
	const struct command *cmd = command_named(name);
	if (cmd == NULL)
	{
		Printf((CONST_STRPTR) "VCHIQTest: no command %s (VCHIQTest ? lists them)\n", name);
		goto out_top;
	}
	if (rest != NULL && strcmp(rest, "?") == 0)
	{
		Printf((CONST_STRPTR) "%s %s\n", cmd->name, cmd->template);
		rc = RETURN_OK;
		goto out_top;
	}
	LONG args[ARGS_MAX];
	for (ULONG i = 0; i < ARGS_MAX; i++)
		args[i] = 0;
	struct RDArgs *rda_cmd = NULL;
	if (cmd->template[0] != 0)
	{
		rda_cmd = parse_rest(cmd->template, rest, args);
		if (rda_cmd == NULL)
		{
			PrintFault(IoErr(), (CONST_STRPTR) "VCHIQTest");
			goto out_top;
		}
	}

	kv_debug_log("VCHIQTest", (const char *)GetArgStr(), FALSE);
	kv_begin(cmd->name);
	kv_num("cli", ((struct Process *)FindTask(NULL))->pr_TaskNum);
	rc = RETURN_ERROR;
	static struct tool t; /* big: the service's message buffers */
	memset(&t, 0, sizeof(t));
	t.waiter.sig = -1;
	if (!waiter_open(&t.waiter))
	{
		kv_str("fail", "no signal or timer");
		goto out_run;
	}
	rc = cmd->run(&t, args);
	waiter_close(&t.waiter);
out_run:
	rc = kv_end(rc);
	if (rda_cmd != NULL)
	{
		FreeArgs(rda_cmd);
		FreeDosObject(DOS_RDARGS, rda_cmd);
	}
out_top:
	FreeArgs(rda);
	return rc;
}
