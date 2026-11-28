/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * commands.h - VCHIQTest's commands: what they share, and each one's template
 * and entry point.  A command reads its own arguments by an enum that
 * matches its template (one file each), prints its facts as key=value
 * (kv.h) and returns an AmigaDOS code:
 *
 *   RETURN_OK     done, and the command's own checks passed
 *   RETURN_WARN   done, a self-check failed (fail=)
 *   RETURN_ERROR  the environment: a library, device, file or memory
 *                 missing, or no test hooks (fail=)
 *   RETURN_FAIL   the API refused or failed (error=), nothing happened for
 *                 IDLE_MS, or the arguments made no sense (fail=)
 */
#ifndef VCHIQTEST_COMMANDS_H
#define VCHIQTEST_COMMANDS_H

#include <exec/types.h>
#include <exec/libraries.h>

#include <waiter.h>
#include <vchiq_client.h>

#define IDLE_MS 5000 /* nothing moved for this long: stuck */
#define ARGS_MAX 28  /* the longest template's entries; parse_rest() refuses a longer one */

/* What every command gets */
struct tool
{
	struct waiter waiter;
	struct vq_service svc;
};

typedef int (*command_fn)(struct tool *t, LONG *args);

extern const char status_template[];
extern const char abandon_template[];
extern const char audio_template[];
extern const char ahiwalk_template[];
extern const char ahitone_template[];
extern const char device_template[];

int do_status(struct tool *t, LONG *args);
int do_abandon(struct tool *t, LONG *args);
int do_audio(struct tool *t, LONG *args);
int do_ahiwalk(struct tool *t, LONG *args);
int do_ahitone(struct tool *t, LONG *args);
int do_device(struct tool *t, LONG *args);

#endif /* VCHIQTEST_COMMANDS_H */
