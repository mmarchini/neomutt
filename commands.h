/**
 * @file
 * Manage where the email is piped to external commands
 *
 * @authors
 * Copyright (C) 2018 Richard Russon <rich@flatcap.org>
 *
 * @copyright
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MUTT_COMMANDS_H
#define MUTT_COMMANDS_H

#include <stdbool.h>
#include <stdio.h>

struct Body;
struct Email;
struct EmailList;
struct Envelope;
struct Mailbox;

/* These Config Variables are only used in commands.c */
extern unsigned char CryptVerifySig; /* verify PGP signatures */
extern char *        DisplayFilter;
extern bool          PipeDecode;
extern char *        PipeSep;
extern bool          PipeSplit;
extern bool          PrintDecode;
extern bool          PrintSplit;
extern bool          PromptAfter;

void ci_bounce_message(struct Mailbox *m, struct EmailList *el);
void mutt_check_stats(void);
bool mutt_check_traditional_pgp(struct EmailList *el, int *redraw);
void mutt_display_address(struct Envelope *env);
int  mutt_display_message(struct Email *cur);
int  mutt_edit_content_type(struct Email *e, struct Body *b, FILE *fp);
void mutt_enter_command(void);
void mutt_pipe_message(struct Email *e);
void mutt_print_message(struct Email *e);
int  mutt_save_message_ctx(struct Email *e, bool delete, bool decode, bool decrypt, struct Mailbox *m);
int  mutt_save_message(struct Mailbox *m, struct EmailList *el, bool delete, bool decode, bool decrypt);
int  mutt_select_sort(int reverse);
void mutt_shell_escape(void);

#endif /* MUTT_COMMANDS_H */
