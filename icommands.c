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

#include "config.h"

#include "icommands.h"
#include "summary.h"

/* prototypes for interactive commands */
static int icmd_test(BUFFER *, BUFFER *, unsigned long, BUFFER *);
static int icmd_quit(BUFFER *, BUFFER *, unsigned long, BUFFER *);
static int icmd_bind(BUFFER *, BUFFER *, unsigned long, BUFFER *);
static int icmd_color(BUFFER *, BUFFER *, unsigned long, BUFFER *);
static int icmd_help(BUFFER *, BUFFER *, unsigned long, BUFFER *);
static int icmd_messages(BUFFER *, BUFFER *, unsigned long, BUFFER *);
static int icmd_scripts(BUFFER *, BUFFER *, unsigned long, BUFFER *);
static int icmd_version(BUFFER *, BUFFER *, unsigned long, BUFFER *);
static int icmd_vars(BUFFER *, BUFFER *, unsigned long, BUFFER *);
/* WARNING: set is already defined and would be overriden, therfore changed name to vars */

/* lookup table for all available interactive commands
 * be aware, that these command take precendence over conventional mutt rc-lines
 */
const struct icommand_t ICommands[] = {
    {"test", icmd_test, 0},
    {"quit", icmd_quit, 0},
    {"q", icmd_quit, 0},
    {"q!", icmd_quit, 0},
    {"qa", icmd_quit, 0},
    {"bind", icmd_bind, 0},
    {"macro", icmd_bind, 0},
    {"color", icmd_color, 0},
    {"help", icmd_help, 0},
    {"messages", icmd_messages, 0},
    {"scripts", icmd_scripts, 0},
    {"version", icmd_version, 0},
    {"vars", icmd_vars, 0},
    {NULL, NULL, 0} /* important for end of loop conditions */
};

/* WARNING: this is a simplified clone from init.c:mutt_parse_command   */
/* TODO: replace it with function more appropriate/optimized for ICommands */
/*
 * line     command to execute
 * err      where to write error messages
 */
int neomutt_parse_icommand(/* const */ char *line, BUFFER *err)
{
  int i, r = 0;

  BUFFER expn, token;

  if (!line || !*line)
    return 0;

  mutt_buffer_init(&expn);
  mutt_buffer_init(&token);

  expn.data = expn.dptr = line;
  expn.dsize = mutt_strlen(line);

  *err->data = 0;

  SKIPWS(expn.dptr);
  while (*expn.dptr)
  {
    /* TODO: contemplate implementing a icommand specific tokenizer */
    mutt_extract_token(&token, &expn, 0);
    for (i = 0; ICommands[i].name; i++)
    {
      if (!mutt_strcmp(token.data, ICommands[i].name))
      {
        r = ICommands[i].func(&token, &expn, ICommands[i].data, err);
        if (r != 0)
        {              /* -1 Error, +1 Finish */
          goto finish; /* Propagate return code */
        }
        break; /* Continue with next command */
      }
    }

    /* command not found */
    if (!ICommands[i].name)
    {
      snprintf(err->data, err->dsize, ICOMMAND_NOT_FOUND, NONULL(token.data));
      r = -1;
    }
  }
finish:
  if (expn.destroy)
    FREE(&expn.data);
  return r;
}


/*
 *  wrapper functions to prepare and call other functionality within mutt
 *  see icmd_quit and icmd_help for easy examples
 */
static int icmd_quit(BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  /* TODO: exit more gracefully */
  mutt_exit(0);
  /* dummy pedantic gcc, needs an int return value :-) */
  return 1;
}

static int icmd_help(BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  /* TODO: implement ':help' command as suggested by flatcap in #162 */
  snprintf(err->data, err->dsize, _("Not implemented yet."));
  return 1;
}

static int icmd_test(BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  mutt_summary();
  return 0;
}

static int icmd_bind(BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  /* TODO: implement ':bind' and ':macro' command as suggested by flatcap in #162 */
  snprintf(err->data, err->dsize, _("Not implemented yet."));
  return 1;
}
static int icmd_color(BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)

{
  /* TODO: implement ':color' command as suggested by flatcap in #162 */
  snprintf(err->data, err->dsize, _("Not implemented yet."));
  return 1;
}

static int icmd_messages(BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  /* TODO: implement ':messages' command as suggested by flatcap in #162 */
  snprintf(err->data, err->dsize, _("Not implemented yet."));
  return 1;
}

static int icmd_scripts(BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  /* TODO: implement ':scripts' command as suggested by flatcap in #162 */
  snprintf(err->data, err->dsize, _("Not implemented yet."));
  return 1;
}

static int icmd_vars(BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  /* TODO: implement ':vars' command as suggested by flatcap in #162 */
  snprintf(err->data, err->dsize, _("Not implemented yet."));
  return 1;
}

static int icmd_version(BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  /* TODO: implement ':version' command as suggested by flatcap in #162 */
  snprintf(err->data, err->dsize, _("Not implemented yet."));
  return 1;
}
