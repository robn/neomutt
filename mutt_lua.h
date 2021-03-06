/**
 * Copyright (C) 2016 Richard Russon <rich@flatcap.org>
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

#ifndef _MUTT_LUA_H
#define _MUTT_LUA_H 1

#include "mutt.h"

int mutt_lua_parse(BUFFER *tmp, BUFFER *s, unsigned long data, BUFFER *err);
int mutt_lua_source_file(BUFFER *tmp, BUFFER *s, unsigned long data, BUFFER *err);

#endif /* _MUTT_LUA_H */
