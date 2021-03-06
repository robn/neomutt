/**
 * Copyright (C) 2016 Bernard Pratz <z+mutt+pub@m0g.net>
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

#ifndef _MUTT_COMMANDS_H
#define _MUTT_COMMANDS_H 1

#include "mutt.h"

struct command_t
{
  char *name;
  int (*func)(BUFFER *, BUFFER *, unsigned long, BUFFER *);
  unsigned long data;
};

const struct command_t *mutt_command_get(const char *s);
void mutt_commands_apply(void *data, void (*application)(void *, const struct command_t *));

#endif /* _MUTT_COMMANDS_H */
