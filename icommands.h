/*
 * Copyright (C) 2016 Christopher John CZETTEL <chris@meicloud.at>
 *
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef _ICOMMAND_H
#define _ICOMMAND_H

static const char *ICOMMAND_NOT_FOUND = "ICOMMAND_NOT_FOUND";

#include <ctype.h>
#include <stdio.h>

#include "mutt.h"

struct icommand_t
{
  char *name;
  int (*func)(BUFFER *, BUFFER *, unsigned long, BUFFER *);
  unsigned long data;
};


int neomutt_parse_icommand(/* const */ char *line, BUFFER *err);

#endif
