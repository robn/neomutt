/**
 * Copyright (C) 1996-2000 Michael R. Elkins <me@mutt.org>
 *
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

/* common protos for compose / attach menus */

#ifndef _MUTT_ATTACH_H
#define _MUTT_ATTACH_H 1

#include "mutt_menu.h"

typedef struct attachptr
{
  BODY *content;
  int parent_type;
  char *tree;
  int level;
  int num;
  bool unowned : 1;   /* don't unlink on detach */
} ATTACHPTR;

ATTACHPTR **mutt_gen_attach_list(BODY *m, int parent_type, ATTACHPTR **idx,
                                 short *idxlen, short *idxmax, int level, int compose);
void mutt_update_tree(ATTACHPTR **idx, short idxlen);
int mutt_view_attachment(FILE *fp, BODY *a, int flag, HEADER *hdr,
                         ATTACHPTR **idx, short idxlen);

int mutt_tag_attach(MUTTMENU *menu, int n, int m);
int mutt_attach_display_loop(MUTTMENU *menu, int op, FILE *fp, HEADER *hdr, BODY *cur,
                         ATTACHPTR ***idxp, short *idxlen, short *idxmax, int recv);
int mutt_view_in_background(char *command);

void mutt_save_attachment_list(FILE *fp, int tag, BODY *top, HEADER *hdr, MUTTMENU *menu);
void mutt_pipe_attachment_list(FILE *fp, int tag, BODY *top, int filter);
void mutt_print_attachment_list(FILE *fp, int tag, BODY *top);

void mutt_attach_bounce(FILE *fp, HEADER *hdr, ATTACHPTR **idx, short idxlen, BODY *cur);
void mutt_attach_resend(FILE *fp, HEADER *hdr, ATTACHPTR **idx, short idxlen, BODY *cur);
void mutt_attach_forward(FILE *fp, HEADER *hdr, ATTACHPTR **idx, short idxlen,
                         BODY *cur, int flags);
void mutt_attach_reply(FILE *fp, HEADER *hdr, ATTACHPTR **idx, short idxlen,
                       BODY *cur, int flags);

#endif /* _MUTT_ATTACH_H */
