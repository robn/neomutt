/**
 * Copyright (C) 1996-2002,2010,2013,2016 Michael R. Elkins <me@mutt.org>
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

#include "config.h"

#include "mutt.h"
#include "filter.h"
#include "mapping.h"
#include "mutt_curses.h"
#include "mutt_menu.h"
#include "mutt_regex.h"
#include "history.h"
#include "keymap.h"
#include "mbyte.h"
#include "charset.h"
#include "mutt_crypt.h"
#include "mutt_idna.h"
#include "group.h"
#include "version.h"

#ifdef USE_SSL
#include "mutt_ssl.h"
#endif

#ifdef USE_NOTMUCH
#include "mutt_notmuch.h"
#endif

#include "mx.h"
#include "init.h"
#include "mailbox.h"
#include "myvar.h"

#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/utsname.h>
#include <netdb.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/time.h>

#define CHECK_PAGER \
  if ((CurrentMenu == MENU_PAGER) && (idx >= 0) &&	\
	    (MuttVars[idx].flags & R_RESORT)) \
	{ \
	  snprintf (err->data, err->dsize, \
	    _("Not available in this menu.")); \
	  return -1; \
	}

typedef struct myvar
{
  char *name;
  char *value;
  struct myvar* next;
} myvar_t;

static myvar_t* MyVars;

static void myvar_set (const char* var, const char* val)
{
  myvar_t** cur;

  for (cur = &MyVars; *cur; cur = &((*cur)->next))
    if (mutt_strcmp ((*cur)->name, var) == 0)
      break;

  if (!*cur)
    *cur = safe_calloc (1, sizeof (myvar_t));

  if (!(*cur)->name)
    (*cur)->name = safe_strdup (var);

  mutt_str_replace (&(*cur)->value, val);
}

static void myvar_del (const char* var)
{
  myvar_t **cur;
  myvar_t *tmp = NULL;


  for (cur = &MyVars; *cur; cur = &((*cur)->next))
    if (mutt_strcmp ((*cur)->name, var) == 0)
      break;

  if (*cur)
  {
    tmp = (*cur)->next;
    FREE (&(*cur)->name);
    FREE (&(*cur)->value);
    FREE (cur);		/* __FREE_CHECKED__ */
    *cur = tmp;
  }
}


#ifdef USE_NOTMUCH
/* List of tags found in last call to mutt_nm_query_complete(). */
static char **nm_tags;
#endif


extern char **envlist;

static void toggle_quadoption (int opt)
{
  int n = opt/4;
  int b = (opt % 4) * 2;

  QuadOptions[n] ^= (1 << b);
}

static int parse_regex(int idx, BUFFER *tmp, BUFFER *err)
{
  int e, flags = 0;
  const char *p = NULL;
  regex_t *rx = NULL;
  REGEXP *ptr = (REGEXP *) MuttVars[idx].data;

  if (!ptr->pattern || (mutt_strcmp(ptr->pattern, tmp->data) != 0))
  {
    int not = 0;

    /* $mask is case-sensitive */
    if (mutt_strcmp(MuttVars[idx].option, "mask") != 0)
      flags |= mutt_which_case(tmp->data);

    p = tmp->data;
    if (mutt_strcmp(MuttVars[idx].option, "mask") == 0)
    {
      if (*p == '!')
      {
        not = 1;
        p++;
      }
    }

    rx = safe_malloc(sizeof(regex_t));
    if ((e = REGCOMP(rx, p, flags)) != 0)
    {
      regerror(e, rx, err->data, err->dsize);
      FREE(&rx);
      return 0;
    }

    /* get here only if everything went smoothly */
    if (ptr->pattern)
    {
      FREE(&ptr->pattern);
      regfree((regex_t *) ptr->rx);
      FREE(&ptr->rx);
    }

    ptr->pattern = safe_strdup(tmp->data);
    ptr->rx = rx;
    ptr->not = not;

    return 1;
  }
  return 0;
}


void set_quadoption (int opt, int flag)
{
  int n = opt/4;
  int b = (opt % 4) * 2;

  QuadOptions[n] &= ~(0x3 << b);
  QuadOptions[n] |= (flag & 0x3) << b;
}

int quadoption (int opt)
{
  int n = opt/4;
  int b = (opt % 4) * 2;

  return (QuadOptions[n] >> b) & 0x3;
}

int query_quadoption (int opt, const char *prompt)
{
  int v = quadoption (opt);

  switch (v)
  {
    case MUTT_YES:
    case MUTT_NO:
      return v;

    default:
      v = mutt_yesorno (prompt, (v == MUTT_ASKYES));
      mutt_window_clearline (MuttMessageWindow, 0);
      return v;
  }

  /* not reached */
}

/* given the variable ``s'', return the index into the rc_vars array which
   matches, or -1 if the variable is not found.  */
int mutt_option_index (const char *s)
{
  int i;

  for (i = 0; MuttVars[i].option; i++)
    if (mutt_strcmp (s, MuttVars[i].option) == 0)
      return (MuttVars[i].type == DT_SYN ?  mutt_option_index ((char *) MuttVars[i].data) : i);
  return -1;
}

#ifdef USE_LUA
int mutt_option_to_string(const struct option_t *opt, char *val, size_t len)
{
  mutt_debug(2, " * mutt_option_to_string(%s)\n",
             NONULL((char *) opt->data));
  int idx = mutt_option_index((const char *) opt->option);
  if (idx != -1)
    return var_to_string(idx, val, len);
  return 0;
}

const struct option_t *mutt_option_get(const char *s)
{
  mutt_debug(2, " * mutt_option_get(%s)\n", s);
  int idx = mutt_option_index(s);
  if (idx != -1)
    return &MuttVars[idx];
  else if (mutt_strncmp("my_", s, 3) == 0)
  {
    struct option_t *opt = safe_malloc(sizeof(struct option_t));
    if (!myvar_get(s))
        return NULL;
    opt->data = (unsigned long) safe_strdup(myvar_get(s));
    if (*((char **) opt->data))
    {
      opt->option = safe_strdup(s);
      opt->type = DT_STR;
      return opt;
    }
    FREE(&opt);
  }
  return NULL;
}
#endif

static void free_mbchar_table (mbchar_table **t)
{
  if (!t || !*t)
    return;

  FREE (&(*t)->chars);
  FREE (&(*t)->segmented_str);
  FREE (&(*t)->orig_str);
  FREE (t);		/* __FREE_CHECKED__ */
}

static mbchar_table *parse_mbchar_table (const char *s)
{
  mbchar_table *t = NULL;
  size_t slen, k;
  mbstate_t mbstate;
  char *d = NULL;

  t = safe_calloc (1, sizeof (mbchar_table));
  slen = mutt_strlen (s);
  if (!slen)
    return t;

  t->orig_str = safe_strdup (s);
  /* This could be more space efficient.  However, being used on tiny
   * strings (Tochars and StChars), the overhead is not great. */
  t->chars = safe_calloc (slen, sizeof (char *));
  d = t->segmented_str = safe_calloc (slen * 2, sizeof (char));

  memset (&mbstate, 0, sizeof (mbstate));
  while (slen && (k = mbrtowc (NULL, s, slen, &mbstate)))
  {
    if (k == (size_t)(-1) || k == (size_t)(-2))
    {
      mutt_debug (1,
                  "parse_mbchar_table: mbrtowc returned %d converting %s in %s\n",
                  (k == (size_t)(-1)) ? -1 : -2, s, t->orig_str);
      if (k == (size_t)(-1))
        memset (&mbstate, 0, sizeof (mbstate));
      k = (k == (size_t)(-1)) ? 1 : slen;
    }

    slen -= k;
    t->chars[t->len++] = d;
    while (k--)
      *d++ = *s++;
    *d++ = '\0';
  }

  return t;
}

static int
parse_sort (short *val, const char *s, const struct mapping_t *map, BUFFER *err)
{
  int i, flags = 0;

  if (mutt_strncmp ("reverse-", s, 8) == 0)
  {
    s += 8;
    flags = SORT_REVERSE;
  }

  if (mutt_strncmp ("last-", s, 5) == 0)
  {
    s += 5;
    flags |= SORT_LAST;
  }

  if ((i = mutt_getvaluebyname (s, map)) == -1)
  {
    snprintf (err->data, err->dsize, _("%s: unknown sorting method"), s);
    return -1;
  }

  *val = i | flags;

  return 0;
}

#ifdef USE_LUA
int mutt_option_set(const struct option_t *val, BUFFER *err)
{
  mutt_debug(2, " * mutt_option_set()\n");
  int idx = mutt_option_index(val->option);
  if (idx != -1)
  {
    switch (DTYPE(MuttVars[idx].type))
    {
      case DT_RX:
      {
        BUFFER *err2 = safe_malloc(sizeof(BUFFER));
        BUFFER tmp;
        tmp.data = safe_strdup((char *) val->data);
        tmp.dsize = strlen((char *) val->data);

        if (parse_regex(idx, &tmp, err2))
        {
          /* $reply_regexp and $alternates require special treatment */
          if (Context && Context->msgcount &&
              (mutt_strcmp(MuttVars[idx].option, "reply_regexp") == 0))
          {
            regmatch_t pmatch[1];
            int i;

#define CUR_ENV Context->hdrs[i]->env
            for (i = 0; i < Context->msgcount; i++)
            {
              if (CUR_ENV && CUR_ENV->subject)
              {
                CUR_ENV->real_subj =
                    (regexec(ReplyRegexp.rx, CUR_ENV->subject, 1, pmatch, 0)) ?
                        CUR_ENV->subject :
                        CUR_ENV->subject + pmatch[0].rm_eo;
              }
            }
#undef CUR_ENV
          }
        }
        else
        {
          snprintf(err2->data, err2->dsize, _("%s: Unknown type."),
                   MuttVars[idx].option);
          return -1;
        }
        FREE(&tmp.data);
        break;
      }
      case DT_SORT:
      {
        const struct mapping_t *map = NULL;
        BUFFER *err2 = safe_malloc(sizeof(BUFFER));

        switch (MuttVars[idx].type & DT_SUBTYPE_MASK)
        {
          case DT_SORT_ALIAS:
            map = SortAliasMethods;
            break;
          case DT_SORT_BROWSER:
            map = SortBrowserMethods;
            break;
          case DT_SORT_KEYS:
            if ((WithCrypto & APPLICATION_PGP))
              map = SortKeyMethods;
            break;
          case DT_SORT_AUX:
            map = SortAuxMethods;
            break;
          case DT_SORT_SIDEBAR:
            map = SortSidebarMethods;
            break;
          default:
            map = SortMethods;
            break;
        }

        if (!map)
        {
          snprintf(err2->data, err2->dsize, _("%s: Unknown type."),
                   MuttVars[idx].option);
          return -1;
        }

        if (parse_sort((short *) MuttVars[idx].data, (const char *) val->data,
                       map, err2) == -1)
          return -1;
      }
      break;
      case DT_MBCHARTBL:
      {
        mbchar_table **tbl = (mbchar_table **) MuttVars[idx].data;
        free_mbchar_table(tbl);
        *tbl = parse_mbchar_table((const char *) val->data);
      }
      break;
      case DT_ADDR:
        rfc822_free_address((ADDRESS **) MuttVars[idx].data);
        *((ADDRESS **) MuttVars[idx].data) =
            rfc822_parse_adrlist(NULL, (const char *) val->data);
        break;
      case DT_PATH:
      {
        char scratch[LONG_STRING];
        strfcpy(scratch, (const char *) val->data, sizeof(scratch));
        mutt_expand_path(scratch, sizeof(scratch));
        /* MuttVars[idx].data is already 'char**' (or some 'void**') or...
        * so cast to 'void*' is okay */
        FREE((void *) MuttVars[idx].data); /* __FREE_CHECKED__ */
        *((char **) MuttVars[idx].data) = safe_strdup(scratch);
        break;
      }
      case DT_STR:
      {
        /* MuttVars[idx].data is already 'char**' (or some 'void**') or...
          * so cast to 'void*' is okay */
        FREE((void *) MuttVars[idx].data); /* __FREE_CHECKED__ */
        *((char **) MuttVars[idx].data) = safe_strdup((char *) val->data);
      }
      break;
      case DT_BOOL:
        if (val->data)
          set_option(MuttVars[idx].data);
        else
          unset_option(MuttVars[idx].data);
        break;
      case DT_QUAD:
        set_quadoption(MuttVars[idx].data, val->data);
        break;
      case DT_NUM:
        *((short *) MuttVars[idx].data) = val->data;
        break;
      default:
        return -1;
    }
  }
  /* set the string as a myvar if it's one */
  if (mutt_strncmp("my_", val->option, 3) == 0)
  {
    myvar_set(val->option, (const char *) val->data);
  }
  return 0;
}
#endif

static void free_opt (struct option_t* p)
{
  REGEXP* pp = NULL;

  switch (p->type & DT_MASK)
  {
  case DT_ADDR:
    rfc822_free_address ((ADDRESS**)p->data);
    break;
  case DT_RX:
    pp = (REGEXP*)p->data;
    FREE (&pp->pattern);
    if (pp->rx)
    {
      regfree (pp->rx);
      FREE (&pp->rx);
    }
    break;
  case DT_PATH:
  case DT_STR:
    FREE ((char**)p->data);		/* __FREE_CHECKED__ */
    break;
  }
}

/* clean up before quitting */
void mutt_free_opts (void)
{
  int i;

  for (i = 0; MuttVars[i].option; i++)
    free_opt (MuttVars + i);

  mutt_free_rx_list (&Alternates);
  mutt_free_rx_list (&UnAlternates);
  mutt_free_rx_list (&MailLists);
  mutt_free_rx_list (&UnMailLists);
  mutt_free_rx_list (&SubscribedLists);
  mutt_free_rx_list (&UnSubscribedLists);
  mutt_free_rx_list (&NoSpamList);
}

static void add_to_list (LIST **list, const char *str)
{
  LIST *t = NULL, *last = NULL;

  /* don't add a NULL or empty string to the list */
  if (!str || *str == '\0')
    return;

  /* check to make sure the item is not already on this list */
  for (last = *list; last; last = last->next)
  {
    if (ascii_strcasecmp (str, last->data) == 0)
    {
      /* already on the list, so just ignore it */
      last = NULL;
      break;
    }
    if (!last->next)
      break;
  }

  if (!*list || last)
  {
    t = safe_calloc (1, sizeof (LIST));
    t->data = safe_strdup (str);
    if (last)
    {
      last->next = t;
      last = last->next;
    }
    else
      *list = last = t;
  }
}

static RX_LIST *new_rx_list(void)
{
  return safe_calloc (1, sizeof (RX_LIST));
}

int mutt_add_to_rx_list (RX_LIST **list, const char *s, int flags, BUFFER *err)
{
  RX_LIST *t = NULL, *last = NULL;
  REGEXP *rx = NULL;

  if (!s || !*s)
    return 0;

  if (!(rx = mutt_compile_regexp (s, flags)))
  {
    snprintf (err->data, err->dsize, "Bad regexp: %s\n", s);
    return -1;
  }

  /* check to make sure the item is not already on this list */
  for (last = *list; last; last = last->next)
  {
    if (ascii_strcasecmp (rx->pattern, last->rx->pattern) == 0)
    {
      /* already on the list, so just ignore it */
      last = NULL;
      break;
    }
    if (!last->next)
      break;
  }

  if (!*list || last)
  {
    t = new_rx_list();
    t->rx = rx;
    if (last)
    {
      last->next = t;
      last = last->next;
    }
    else
      *list = last = t;
  }
  else /* duplicate */
    mutt_free_regexp (&rx);

  return 0;
}

static int remove_from_replace_list (REPLACE_LIST **list, const char *pat)
{
  REPLACE_LIST *cur = NULL, *prev = NULL;
  int nremoved = 0;

  /* Being first is a special case. */
  cur = *list;
  if (!cur)
    return 0;
  if (cur->rx && (mutt_strcmp(cur->rx->pattern, pat) == 0))
  {
    *list = cur->next;
    mutt_free_regexp(&cur->rx);
    FREE(&cur->template);
    FREE(&cur);
    return 1;
  }

  prev = cur;
  for (cur = prev->next; cur;)
  {
    if (mutt_strcmp(cur->rx->pattern, pat) == 0)
    {
      prev->next = cur->next;
      mutt_free_regexp(&cur->rx);
      FREE(&cur->template);
      FREE(&cur);
      cur = prev->next;
      ++nremoved;
    }
    else
      cur = cur->next;
  }

  return nremoved;
}

static REPLACE_LIST *new_replace_list(void)
{
  return safe_calloc (1, sizeof (REPLACE_LIST));
}

static int add_to_replace_list (REPLACE_LIST **list, const char *pat, const char *templ, BUFFER *err)
{
  REPLACE_LIST *t = NULL, *last = NULL;
  REGEXP *rx = NULL;
  int n;
  const char *p = NULL;

  if (!pat || !*pat || !templ)
    return 0;

  if (!(rx = mutt_compile_regexp (pat, REG_ICASE)))
  {
    snprintf (err->data, err->dsize, _("Bad regexp: %s"), pat);
    return -1;
  }

  /* check to make sure the item is not already on this list */
  for (last = *list; last; last = last->next)
  {
    if (ascii_strcasecmp (rx->pattern, last->rx->pattern) == 0)
    {
      /* Already on the list. Formerly we just skipped this case, but
       * now we're supporting removals, which means we're supporting
       * re-adds conceptually. So we probably want this to imply a
       * removal, then do an add. We can achieve the removal by freeing
       * the template, and leaving t pointed at the current item.
       */
      t = last;
      FREE(&t->template);
      break;
    }
    if (!last->next)
      break;
  }

  /* If t is set, it's pointing into an extant REPLACE_LIST* that we want to
   * update. Otherwise we want to make a new one to link at the list's end.
   */
  if (!t)
  {
    t = new_replace_list();
    t->rx = rx;
    rx = NULL;
    if (last)
      last->next = t;
    else
      *list = t;
  }
  else
    mutt_free_regexp(&rx);

  /* Now t is the REPLACE_LIST* that we want to modify. It is prepared. */
  t->template = safe_strdup(templ);

  /* Find highest match number in template string */
  t->nmatch = 0;
  for (p = templ; *p;)
  {
    if (*p == '%')
    {
        n = atoi(++p);
        if (n > t->nmatch)
          t->nmatch = n;
        while (*p && isdigit((int)*p))
          ++p;
    }
    else
        ++p;
  }

  if (t->nmatch > t->rx->rx->re_nsub)
  {
    snprintf (err->data, err->dsize, _("Not enough subexpressions for "
                                       "template"));
    remove_from_replace_list(list, pat);
    return -1;
  }

  t->nmatch++;         /* match 0 is always the whole expr */

  return 0;
}


static void remove_from_list (LIST **l, const char *str)
{
  LIST *p = NULL, *last = NULL;

  if (mutt_strcmp ("*", str) == 0)
    mutt_free_list (l);    /* ``unCMD *'' means delete all current entries */
  else
  {
    p = *l;
    last = NULL;
    while (p)
    {
      if (ascii_strcasecmp (str, p->data) == 0)
      {
	FREE (&p->data);
	if (last)
	  last->next = p->next;
	else
	  (*l) = p->next;
	FREE (&p);
      }
      else
      {
	last = p;
	p = p->next;
      }
    }
  }
}

/**
 * finish_source - 'finish' command: stop processing current config file
 * @tmp:  Temporary space shared by all command handlers
 * @s:    Current line of the config file
 * @data: data field from init.h:struct command_t
 * @err:  Buffer for any error message
 *
 * If the 'finish' command is found, we should stop reading the current file.
 *
 * Returns:
 *       1 Stop processing the current file
 *      -1 Failed
 */
static int finish_source (BUFFER *tmp, BUFFER *s, unsigned long data, BUFFER *err)
{
  if (MoreArgs (s))
  {
    snprintf (err->data, err->dsize, _("finish: too many arguments"));
    return -1;
  }

  return 1;
}

/**
 * parse_ifdef - 'ifdef' command: conditional config
 * @tmp:  Temporary space shared by all command handlers
 * @s:    Current line of the config file
 * @data: data field from init.h:struct command_t
 * @err:  Buffer for any error message
 *
 * The 'ifdef' command allows conditional elements in the config file.
 * If a given variable, function, command or compile-time symbol exists, then
 * read the rest of the line of config commands.
 * e.g.
 *      ifdef USE_SIDEBAR source ~/.mutt/sidebar.rc
 *
 * If (data == 1) then it means use the 'ifndef' (if-not-defined) command.
 * e.g.
 *      ifndef USE_IMAP finish
 *
 * Returns:
 *       0 Success
 *      -1 Failed
 */
static int parse_ifdef (BUFFER *tmp, BUFFER *s, unsigned long data, BUFFER *err)
{
  int i, j, res = 0;
  BUFFER token;

  memset (&token, 0, sizeof (token));
  mutt_extract_token (tmp, s, 0);

  /* is the item defined as a variable? */
  res = (mutt_option_index (tmp->data) != -1);

  /* is the item a compiled-in feature? */
  if (!res)
  {
    res = feature_enabled (tmp->data);
  }

  /* or a function? */
  if (!res)
  {
    for (i = 0; !res && (i < MENU_MAX); i++)
    {
      const struct binding_t *b = km_get_table (Menus[i].value);
      if (!b)
        continue;

      for (j = 0; b[j].name; j++)
      {
        if (mutt_strcmp (tmp->data, b[j].name) == 0)
        {
          res = 1;
          break;
        }
      }
    }
  }

  /* or a command? */
  if (!res)
  {
    for (i = 0; Commands[i].name; i++)
    {
      if (mutt_strcmp (tmp->data, Commands[i].name) == 0)
      {
        res = 1;
        break;
      }
    }
  }

  if (!MoreArgs (s))
  {
    snprintf (err->data, err->dsize, _("%s: too few arguments"),
      (data ? "ifndef" : "ifdef"));
    return -1;
  }
  mutt_extract_token (tmp, s, MUTT_TOKEN_SPACE);

  /* ifdef KNOWN_SYMBOL or ifndef UNKNOWN_SYMBOL */
  if ((res && (data == 0)) || (!res && (data == 1)))
  {
                int rc = mutt_parse_rc_line (tmp->data, &token, err);
    if (rc == -1)
    {
      mutt_error ("Error: %s", err->data);
      FREE(&token.data);
      return -1;
    }
    FREE(&token.data);
                return rc;
  }
  return 0;
}

static int parse_unignore (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  do
  {
    mutt_extract_token (buf, s, 0);

    /* don't add "*" to the unignore list */
    if (strcmp (buf->data, "*") != 0)
      add_to_list (&UnIgnore, buf->data);

    remove_from_list (&Ignore, buf->data);
  }
  while (MoreArgs (s));

  return 0;
}

static int parse_ignore (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  do
  {
    mutt_extract_token (buf, s, 0);
    remove_from_list (&UnIgnore, buf->data);
    add_to_list (&Ignore, buf->data);
  }
  while (MoreArgs (s));

  return 0;
}

static int parse_list (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  do
  {
    mutt_extract_token (buf, s, 0);
    add_to_list ((LIST **) data, buf->data);
  }
  while (MoreArgs (s));

  return 0;
}

static void _alternates_clean (void)
{
  int i;
  if (Context && Context->msgcount)
  {
    for (i = 0; i < Context->msgcount; i++)
      Context->hdrs[i]->recip_valid = false;
  }
}

static int parse_alternates (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  group_context_t *gc = NULL;

  _alternates_clean();

  do
  {
    mutt_extract_token (buf, s, 0);

    if (parse_group_context (&gc, buf, s, data, err) == -1)
      goto bail;

    mutt_remove_from_rx_list (&UnAlternates, buf->data);

    if (mutt_add_to_rx_list (&Alternates, buf->data, REG_ICASE, err) != 0)
      goto bail;

    if (mutt_group_context_add_rx (gc, buf->data, REG_ICASE, err) != 0)
      goto bail;
  }
  while (MoreArgs (s));

  mutt_group_context_destroy (&gc);
  return 0;

 bail:
  mutt_group_context_destroy (&gc);
  return -1;
}

static int parse_unalternates (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  _alternates_clean();
  do
  {
    mutt_extract_token (buf, s, 0);
    mutt_remove_from_rx_list (&Alternates, buf->data);

    if ((mutt_strcmp (buf->data, "*") != 0) &&
	mutt_add_to_rx_list (&UnAlternates, buf->data, REG_ICASE, err) != 0)
      return -1;

  }
  while (MoreArgs (s));

  return 0;
}

static int parse_replace_list (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  REPLACE_LIST **list = (REPLACE_LIST **)data;
  BUFFER templ;

  memset(&templ, 0, sizeof(templ));

  /* First token is a regexp. */
  if (!MoreArgs(s))
  {
    strfcpy(err->data, _("not enough arguments"), err->dsize);
    return -1;
  }
  mutt_extract_token(buf, s, 0);

  /* Second token is a replacement template */
  if (!MoreArgs(s))
  {
    strfcpy(err->data, _("not enough arguments"), err->dsize);
    return -1;
  }
  mutt_extract_token(&templ, s, 0);

  if (add_to_replace_list(list, buf->data, templ.data, err) != 0) {
    FREE(&templ.data);
    return -1;
  }
  FREE(&templ.data);

  return 0;
}

static int parse_unreplace_list (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  REPLACE_LIST **list = (REPLACE_LIST **)data;

  /* First token is a regexp. */
  if (!MoreArgs(s))
  {
    strfcpy(err->data, _("not enough arguments"), err->dsize);
    return -1;
  }

  mutt_extract_token(buf, s, 0);

  /* "*" is a special case. */
  if (mutt_strcmp (buf->data, "*") == 0)
  {
    mutt_free_replace_list (list);
    return 0;
  }

  remove_from_replace_list(list, buf->data);
  return 0;
}


static void clear_subject_mods (void)
{
  int i;
  if (Context && Context->msgcount)
  {
    for (i = 0; i < Context->msgcount; i++)
      FREE(&Context->hdrs[i]->env->disp_subj);
  }
}


static int parse_subjectrx_list (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  int rc;

  rc = parse_replace_list(buf, s, data, err);
  if (rc == 0)
    clear_subject_mods();
  return rc;
}


static int parse_unsubjectrx_list (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  int rc;

  rc = parse_unreplace_list(buf, s, data, err);
  if (rc == 0)
    clear_subject_mods();
  return rc;
}


static int parse_spam_list (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  BUFFER templ;

  mutt_buffer_init (&templ);

  /* Insist on at least one parameter */
  if (!MoreArgs(s))
  {
    if (data == MUTT_SPAM)
      strfcpy(err->data, _("spam: no matching pattern"), err->dsize);
    else
      strfcpy(err->data, _("nospam: no matching pattern"), err->dsize);
    return -1;
  }

  /* Extract the first token, a regexp */
  mutt_extract_token (buf, s, 0);

  /* data should be either MUTT_SPAM or MUTT_NOSPAM. MUTT_SPAM is for spam commands. */
  if (data == MUTT_SPAM)
  {
    /* If there's a second parameter, it's a template for the spam tag. */
    if (MoreArgs(s))
    {
      mutt_extract_token (&templ, s, 0);

      /* Add to the spam list. */
      if (add_to_replace_list (&SpamList, buf->data, templ.data, err) != 0) {
	  FREE(&templ.data);
          return -1;
      }
      FREE(&templ.data);
    }

    /* If not, try to remove from the nospam list. */
    else
    {
      mutt_remove_from_rx_list(&NoSpamList, buf->data);
    }

    return 0;
  }

  /* MUTT_NOSPAM is for nospam commands. */
  else if (data == MUTT_NOSPAM)
  {
    /* nospam only ever has one parameter. */

    /* "*" is a special case. */
    if (mutt_strcmp(buf->data, "*") == 0)
    {
      mutt_free_replace_list (&SpamList);
      mutt_free_rx_list (&NoSpamList);
      return 0;
    }

    /* If it's on the spam list, just remove it. */
    if (remove_from_replace_list(&SpamList, buf->data) != 0)
      return 0;

    /* Otherwise, add it to the nospam list. */
    if (mutt_add_to_rx_list (&NoSpamList, buf->data, REG_ICASE, err) != 0)
      return -1;

    return 0;
  }

  /* This should not happen. */
  strfcpy(err->data, "This is no good at all.", err->dsize);
  return -1;
}


static int parse_unlist (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  do
  {
    mutt_extract_token (buf, s, 0);
    /*
     * Check for deletion of entire list
     */
    if (mutt_strcmp (buf->data, "*") == 0)
    {
      mutt_free_list ((LIST **) data);
      break;
    }
    remove_from_list ((LIST **) data, buf->data);
  }
  while (MoreArgs (s));

  return 0;
}

#ifdef USE_SIDEBAR
static int parse_path_list (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  char path[_POSIX_PATH_MAX];

  do
  {
    mutt_extract_token (buf, s, 0);
    strfcpy (path, buf->data, sizeof (path));
    mutt_expand_path (path, sizeof (path));
    add_to_list ((LIST **) data, path);
  }
  while (MoreArgs (s));

  return 0;
}

static int parse_path_unlist (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  char path[_POSIX_PATH_MAX];

  do
  {
    mutt_extract_token (buf, s, 0);
    /*
     * Check for deletion of entire list
     */
    if (mutt_strcmp (buf->data, "*") == 0)
    {
      mutt_free_list ((LIST **) data);
      break;
    }
    strfcpy (path, buf->data, sizeof (path));
    mutt_expand_path (path, sizeof (path));
    remove_from_list ((LIST **) data, path);
  }
  while (MoreArgs (s));

  return 0;
}
#endif

static int parse_lists (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  group_context_t *gc = NULL;

  do
  {
    mutt_extract_token (buf, s, 0);

    if (parse_group_context (&gc, buf, s, data, err) == -1)
      goto bail;

    mutt_remove_from_rx_list (&UnMailLists, buf->data);

    if (mutt_add_to_rx_list (&MailLists, buf->data, REG_ICASE, err) != 0)
      goto bail;

    if (mutt_group_context_add_rx (gc, buf->data, REG_ICASE, err) != 0)
      goto bail;
  }
  while (MoreArgs (s));

  mutt_group_context_destroy (&gc);
  return 0;

 bail:
  mutt_group_context_destroy (&gc);
  return -1;
}

typedef enum group_state_t {
  NONE, RX, ADDR
} group_state_t;

static int parse_group (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  group_context_t *gc = NULL;
  group_state_t state = NONE;
  ADDRESS *addr = NULL;
  char *estr = NULL;

  do
  {
    mutt_extract_token (buf, s, 0);
    if (parse_group_context (&gc, buf, s, data, err) == -1)
      goto bail;

    if (data == MUTT_UNGROUP && (mutt_strcasecmp (buf->data, "*") == 0))
    {
      if (mutt_group_context_clear (&gc) < 0)
	goto bail;
      goto out;
    }

    if (mutt_strcasecmp (buf->data, "-rx") == 0)
      state = RX;
    else if (mutt_strcasecmp (buf->data, "-addr") == 0)
      state = ADDR;
    else
    {
      switch (state)
      {
	case NONE:
	  snprintf (err->data, err->dsize, _("%sgroup: missing -rx or -addr."),
		   data == MUTT_UNGROUP ? "un" : "");
	  goto bail;

	case RX:
	  if (data == MUTT_GROUP &&
	      mutt_group_context_add_rx (gc, buf->data, REG_ICASE, err) != 0)
	    goto bail;
	  else if (data == MUTT_UNGROUP &&
		   mutt_group_context_remove_rx (gc, buf->data) < 0)
	    goto bail;
	  break;

	case ADDR:
	  if ((addr = mutt_parse_adrlist (NULL, buf->data)) == NULL)
	    goto bail;
	  if (mutt_addrlist_to_intl (addr, &estr))
	  {
	    snprintf (err->data, err->dsize, _("%sgroup: warning: bad IDN '%s'.\n"),
		      data == 1 ? "un" : "", estr);
            rfc822_free_address (&addr);
            FREE(&estr);
	    goto bail;
	  }
	  if (data == MUTT_GROUP)
	    mutt_group_context_add_adrlist (gc, addr);
	  else if (data == MUTT_UNGROUP)
	    mutt_group_context_remove_adrlist (gc, addr);
	  rfc822_free_address (&addr);
	  break;
      }
    }
  } while (MoreArgs (s));

out:
  mutt_group_context_destroy (&gc);
  return 0;

bail:
  mutt_group_context_destroy (&gc);
  return -1;
}

/* always wise to do what someone else did before */
static void _attachments_clean (void)
{
  int i;
  if (Context && Context->msgcount)
  {
    for (i = 0; i < Context->msgcount; i++)
      Context->hdrs[i]->attach_valid = false;
  }
}

static int parse_attach_list (BUFFER *buf, BUFFER *s, LIST **ldata, BUFFER *err)
{
  ATTACH_MATCH *a = NULL;
  LIST *listp = NULL, *lastp = NULL;
  char *p = NULL;
  char *tmpminor = NULL;
  int len;
  int ret;

  /* Find the last item in the list that data points to. */
  lastp = NULL;
  mutt_debug (5, "parse_attach_list: ldata = %p, *ldata = %p\n",
              (void *)ldata, (void *)*ldata);
  for (listp = *ldata; listp; listp = listp->next)
  {
    a = (ATTACH_MATCH *)listp->data;
    mutt_debug (5, "parse_attach_list: skipping %s/%s\n",
                a->major, a->minor);
    lastp = listp;
  }

  do
  {
    mutt_extract_token (buf, s, 0);

    if (!buf->data || *buf->data == '\0')
      continue;

    a = safe_malloc(sizeof(ATTACH_MATCH));

    /* some cheap hacks that I expect to remove */
    if (ascii_strcasecmp(buf->data, "any") == 0)
      a->major = safe_strdup("*/.*");
    else if (ascii_strcasecmp(buf->data, "none") == 0)
      a->major = safe_strdup("cheap_hack/this_should_never_match");
    else
      a->major = safe_strdup(buf->data);

    if ((p = strchr(a->major, '/')))
    {
      *p = '\0';
      ++p;
      a->minor = p;
    }
    else
    {
      a->minor = "unknown";
    }

    len = strlen(a->minor);
    tmpminor = safe_malloc(len+3);
    strcpy(&tmpminor[1], a->minor); /* __STRCPY_CHECKED__ */
    tmpminor[0] = '^';
    tmpminor[len+1] = '$';
    tmpminor[len+2] = '\0';

    a->major_int = mutt_check_mime_type(a->major);
    ret = REGCOMP(&a->minor_rx, tmpminor, REG_ICASE);

    FREE(&tmpminor);

    if (ret)
    {
      regerror(ret, &a->minor_rx, err->data, err->dsize);
      FREE(&a->major);
      FREE(&a);
      return -1;
    }

    mutt_debug (5, "parse_attach_list: added %s/%s [%d]\n",
                a->major, a->minor, a->major_int);

    listp = safe_malloc(sizeof(LIST));
    listp->data = (char *)a;
    listp->next = NULL;
    if (lastp)
    {
      lastp->next = listp;
    }
    else
    {
      *ldata = listp;
    }
    lastp = listp;
  }
  while (MoreArgs (s));

  _attachments_clean();
  return 0;
}

static int parse_unattach_list (BUFFER *buf, BUFFER *s, LIST **ldata, BUFFER *err)
{
  ATTACH_MATCH *a = NULL;
  LIST *lp = NULL, *lastp = NULL, *newlp = NULL;
  char *tmp = NULL;
  int major;
  char *minor = NULL;

  do
  {
    mutt_extract_token (buf, s, 0);
    FREE(&tmp);

    if (ascii_strcasecmp(buf->data, "any") == 0)
      tmp = safe_strdup("*/.*");
    else if (ascii_strcasecmp(buf->data, "none") == 0)
      tmp = safe_strdup("cheap_hack/this_should_never_match");
    else
      tmp = safe_strdup(buf->data);

    if ((minor = strchr(tmp, '/')))
    {
      *minor = '\0';
      ++minor;
    }
    else
    {
      minor = "unknown";
    }
    major = mutt_check_mime_type(tmp);

    /* We must do our own walk here because remove_from_list() will only
     * remove the LIST->data, not anything pointed to by the LIST->data. */
    lastp = NULL;
    for(lp = *ldata; lp; )
    {
      a = (ATTACH_MATCH *)lp->data;
      mutt_debug (5, "parse_unattach_list: check %s/%s [%d] : %s/%s [%d]\n",
                  a->major, a->minor, a->major_int, tmp, minor, major);
      if (a->major_int == major && (mutt_strcasecmp(minor, a->minor) == 0))
      {
        mutt_debug (5, "parse_unattach_list: removed %s/%s [%d]\n",
                    a->major, a->minor, a->major_int);
	regfree(&a->minor_rx);
	FREE(&a->major);

	/* Relink backward */
	if (lastp)
	  lastp->next = lp->next;
	else
	  *ldata = lp->next;

        newlp = lp->next;
        FREE(&lp->data);	/* same as a */
        FREE(&lp);
        lp = newlp;
        continue;
      }

      lastp = lp;
      lp = lp->next;
    }

  }
  while (MoreArgs (s));

  FREE(&tmp);
  _attachments_clean();
  return 0;
}

static int print_attach_list (LIST *lp, char op, char *name)
{
  while (lp) {
    printf("attachments %c%s %s/%s\n", op, name,
           ((ATTACH_MATCH *)lp->data)->major,
           ((ATTACH_MATCH *)lp->data)->minor);
    lp = lp->next;
  }

  return 0;
}


static int parse_attachments (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  char op, *category = NULL;
  LIST **listp;

  mutt_extract_token(buf, s, 0);
  if (!buf->data || *buf->data == '\0') {
    strfcpy(err->data, _("attachments: no disposition"), err->dsize);
    return -1;
  }

  category = buf->data;
  op = *category++;

  if (op == '?') {
    mutt_endwin (NULL);
    fflush (stdout);
    printf(_("\nCurrent attachments settings:\n\n"));
    print_attach_list(AttachAllow,   '+', "A");
    print_attach_list(AttachExclude, '-', "A");
    print_attach_list(InlineAllow,   '+', "I");
    print_attach_list(InlineExclude, '-', "I");
    mutt_any_key_to_continue (NULL);
    return 0;
  }

  if (op != '+' && op != '-') {
    op = '+';
    category--;
  }
  if (ascii_strncasecmp(category, "attachment", strlen(category)) == 0) {
    if (op == '+')
      listp = &AttachAllow;
    else
      listp = &AttachExclude;
  }
  else if (ascii_strncasecmp(category, "inline", strlen(category)) == 0) {
    if (op == '+')
      listp = &InlineAllow;
    else
      listp = &InlineExclude;
  }
  else {
    strfcpy(err->data, _("attachments: invalid disposition"), err->dsize);
    return -1;
  }

  return parse_attach_list(buf, s, listp, err);
}

static int parse_unattachments (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  char op, *p = NULL;
  LIST **listp;

  mutt_extract_token(buf, s, 0);
  if (!buf->data || *buf->data == '\0') {
    strfcpy(err->data, _("unattachments: no disposition"), err->dsize);
    return -1;
  }

  p = buf->data;
  op = *p++;
  if (op != '+' && op != '-') {
    op = '+';
    p--;
  }
  if (ascii_strncasecmp(p, "attachment", strlen(p)) == 0) {
    if (op == '+')
      listp = &AttachAllow;
    else
      listp = &AttachExclude;
  }
  else if (ascii_strncasecmp(p, "inline", strlen(p)) == 0) {
    if (op == '+')
      listp = &InlineAllow;
    else
      listp = &InlineExclude;
  }
  else {
    strfcpy(err->data, _("unattachments: invalid disposition"), err->dsize);
    return -1;
  }

  return parse_unattach_list(buf, s, listp, err);
}

static int parse_unlists (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  do
  {
    mutt_extract_token (buf, s, 0);
    mutt_remove_from_rx_list (&SubscribedLists, buf->data);
    mutt_remove_from_rx_list (&MailLists, buf->data);

    if ((mutt_strcmp (buf->data, "*") != 0) &&
	mutt_add_to_rx_list (&UnMailLists, buf->data, REG_ICASE, err) != 0)
      return -1;
  }
  while (MoreArgs (s));

  return 0;
}

static int parse_subscribe (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  group_context_t *gc = NULL;

  do
  {
    mutt_extract_token (buf, s, 0);

    if (parse_group_context (&gc, buf, s, data, err) == -1)
      goto bail;

    mutt_remove_from_rx_list (&UnMailLists, buf->data);
    mutt_remove_from_rx_list (&UnSubscribedLists, buf->data);

    if (mutt_add_to_rx_list (&MailLists, buf->data, REG_ICASE, err) != 0)
      goto bail;
    if (mutt_add_to_rx_list (&SubscribedLists, buf->data, REG_ICASE, err) != 0)
      goto bail;
    if (mutt_group_context_add_rx (gc, buf->data, REG_ICASE, err) != 0)
      goto bail;
  }
  while (MoreArgs (s));

  mutt_group_context_destroy (&gc);
  return 0;

 bail:
  mutt_group_context_destroy (&gc);
  return -1;
}

static int parse_unsubscribe (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  do
  {
    mutt_extract_token (buf, s, 0);
    mutt_remove_from_rx_list (&SubscribedLists, buf->data);

    if ((mutt_strcmp (buf->data, "*") != 0) &&
	mutt_add_to_rx_list (&UnSubscribedLists, buf->data, REG_ICASE, err) != 0)
      return -1;
  }
  while (MoreArgs (s));

  return 0;
}

static int parse_unalias (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  ALIAS *tmp = NULL, *last = NULL;

  do
  {
    mutt_extract_token (buf, s, 0);

    if (mutt_strcmp ("*", buf->data) == 0)
    {
      if (CurrentMenu == MENU_ALIAS)
      {
	for (tmp = Aliases; tmp ; tmp = tmp->next)
	  tmp->del = true;
	mutt_set_current_menu_redraw_full ();
      }
      else
	mutt_free_alias (&Aliases);
      break;
    }
    else
      for (tmp = Aliases; tmp; tmp = tmp->next)
      {
	if (mutt_strcasecmp (buf->data, tmp->name) == 0)
	{
	  if (CurrentMenu == MENU_ALIAS)
	  {
	    tmp->del = true;
	    mutt_set_current_menu_redraw_full ();
	    break;
	  }

	  if (last)
	    last->next = tmp->next;
	  else
	    Aliases = tmp->next;
	  tmp->next = NULL;
	  mutt_free_alias (&tmp);
	  break;
	}
	last = tmp;
      }
  }
  while (MoreArgs (s));
  return 0;
}

static int parse_alias (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  ALIAS *tmp = Aliases;
  ALIAS *last = NULL;
  char *estr = NULL;
  group_context_t *gc = NULL;

  if (!MoreArgs (s))
  {
    strfcpy (err->data, _("alias: no address"), err->dsize);
    return -1;
  }

  mutt_extract_token (buf, s, 0);

  if (parse_group_context (&gc, buf, s, data, err) == -1)
    return -1;

  /* check to see if an alias with this name already exists */
  for (; tmp; tmp = tmp->next)
  {
    if (mutt_strcasecmp (tmp->name, buf->data) == 0)
      break;
    last = tmp;
  }

  if (!tmp)
  {
    /* create a new alias */
    tmp = safe_calloc (1, sizeof (ALIAS));
    tmp->self = tmp;
    tmp->name = safe_strdup (buf->data);
    /* give the main addressbook code a chance */
    if (CurrentMenu == MENU_ALIAS)
      set_option (OPTMENUCALLER);
  }
  else
  {
    mutt_alias_delete_reverse (tmp);
    /* override the previous value */
    rfc822_free_address (&tmp->addr);
    if (CurrentMenu == MENU_ALIAS)
      mutt_set_current_menu_redraw_full ();
  }

  mutt_extract_token (buf, s, MUTT_TOKEN_QUOTE | MUTT_TOKEN_SPACE | MUTT_TOKEN_SEMICOLON);
  mutt_debug (3, "parse_alias: Second token is '%s'.\n", buf->data);

  tmp->addr = mutt_parse_adrlist (tmp->addr, buf->data);

  if (last)
    last->next = tmp;
  else
    Aliases = tmp;
  if (mutt_addrlist_to_intl (tmp->addr, &estr))
  {
    snprintf (err->data, err->dsize, _("Warning: Bad IDN '%s' in alias '%s'.\n"),
	      estr, tmp->name);
    FREE(&estr);
    goto bail;
  }

  mutt_group_context_add_adrlist (gc, tmp->addr);
  mutt_alias_add_reverse (tmp);

#ifdef DEBUG
  if (debuglevel >= 2)
  {
    ADDRESS *a = NULL;
    /* A group is terminated with an empty address, so check a->mailbox */
    for (a = tmp->addr; a && a->mailbox; a = a->next)
    {
      if (!a->group)
        mutt_debug (3, "parse_alias:   %s\n", a->mailbox);
      else
        mutt_debug (3, "parse_alias:   Group %s\n", a->mailbox);
    }
  }
#endif
  mutt_group_context_destroy (&gc);
  return 0;

  bail:
  mutt_group_context_destroy (&gc);
  return -1;
}

static int
parse_unmy_hdr (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  LIST *last = NULL;
  LIST *tmp = UserHeader;
  LIST *ptr = NULL;
  size_t l;

  do
  {
    mutt_extract_token (buf, s, 0);
    if (mutt_strcmp ("*", buf->data) == 0)
      mutt_free_list (&UserHeader);
    else
    {
      tmp = UserHeader;
      last = NULL;

      l = mutt_strlen (buf->data);
      if (buf->data[l - 1] == ':')
	l--;

      while (tmp)
      {
	if ((ascii_strncasecmp (buf->data, tmp->data, l) == 0) && tmp->data[l] == ':')
	{
	  ptr = tmp;
	  if (last)
	    last->next = tmp->next;
	  else
	    UserHeader = tmp->next;
	  tmp = tmp->next;
	  ptr->next = NULL;
	  mutt_free_list (&ptr);
	}
	else
	{
	  last = tmp;
	  tmp = tmp->next;
	}
      }
    }
  }
  while (MoreArgs (s));
  return 0;
}

static int parse_my_hdr (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  LIST *tmp = NULL;
  size_t keylen;
  char *p = NULL;

  mutt_extract_token (buf, s, MUTT_TOKEN_SPACE | MUTT_TOKEN_QUOTE);
  if ((p = strpbrk (buf->data, ": \t")) == NULL || *p != ':')
  {
    strfcpy (err->data, _("invalid header field"), err->dsize);
    return -1;
  }
  keylen = p - buf->data + 1;

  if (UserHeader)
  {
    for (tmp = UserHeader; ; tmp = tmp->next)
    {
      /* see if there is already a field by this name */
      if (ascii_strncasecmp (buf->data, tmp->data, keylen) == 0)
      {
	/* replace the old value */
	FREE (&tmp->data);
	tmp->data = buf->data;
	mutt_buffer_init (buf);
	return 0;
      }
      if (!tmp->next)
	break;
    }
    tmp->next = mutt_new_list ();
    tmp = tmp->next;
  }
  else
  {
    tmp = mutt_new_list ();
    UserHeader = tmp;
  }
  tmp->data = buf->data;
  mutt_buffer_init (buf);
  return 0;
}

static void set_default (struct option_t *p)
{
  switch (p->type & DT_MASK)
  {
    case DT_STR:
      if (!p->init && *((char **) p->data))
        p->init = (unsigned long) safe_strdup (* ((char **) p->data));
      break;
    case DT_PATH:
      if (!p->init && *((char **) p->data))
      {
	char *cp = safe_strdup (*((char **) p->data));
	/* mutt_pretty_mailbox (cp); */
        p->init = (unsigned long) cp;
      }
      break;
    case DT_ADDR:
      if (!p->init && *((ADDRESS **) p->data))
      {
	char tmp[HUGE_STRING];
	*tmp = '\0';
	rfc822_write_address (tmp, sizeof (tmp), *((ADDRESS **) p->data), 0);
	p->init = (unsigned long) safe_strdup (tmp);
      }
      break;
    case DT_RX:
    {
      REGEXP *pp = (REGEXP *) p->data;
      if (!p->init && pp->pattern)
	p->init = (unsigned long) safe_strdup (pp->pattern);
      break;
    }
  }
}

static void restore_default (struct option_t *p)
{
  switch (p->type & DT_MASK)
  {
    case DT_STR:
      mutt_str_replace ((char **) p->data, (char *) p->init);
      break;
    case DT_MBCHARTBL:
      free_mbchar_table ((mbchar_table **)p->data);
      *((mbchar_table **) p->data) = parse_mbchar_table ((char *) p->init);
      break;
    case DT_PATH:
      FREE((char **) p->data);		/* __FREE_CHECKED__ */
      if (p->init)
      {
	char path[_POSIX_PATH_MAX];
	strfcpy (path, (char *) p->init, sizeof (path));
	mutt_expand_path (path, sizeof (path));
	*((char **) p->data) = safe_strdup (path);
      }
      break;
    case DT_ADDR:
      rfc822_free_address ((ADDRESS **) p->data);
      if (p->init)
	*((ADDRESS **) p->data) = rfc822_parse_adrlist (NULL, (char *) p->init);
      break;
    case DT_BOOL:
      if (p->init)
	set_option (p->data);
      else
	unset_option (p->data);
      break;
    case DT_QUAD:
      set_quadoption (p->data, p->init);
      break;
    case DT_NUM:
    case DT_SORT:
    case DT_MAGIC:
      *((short *) p->data) = p->init;
      break;
    case DT_RX:
      {
	REGEXP *pp = (REGEXP *) p->data;
	int flags = 0;

	FREE (&pp->pattern);
	if (pp->rx)
	{
	  regfree (pp->rx);
	  FREE (&pp->rx);
	}

	if (p->init)
	{
	  int retval;
	  char *s = (char *) p->init;

	  pp->rx = safe_calloc (1, sizeof (regex_t));
	  pp->pattern = safe_strdup ((char *) p->init);
	  if (mutt_strcmp (p->option, "mask") != 0)
	    flags |= mutt_which_case ((const char *) p->init);
	  if ((mutt_strcmp (p->option, "mask") == 0) && *s == '!')
	  {
	    s++;
	    pp->not = 1;
	  }
	  retval = REGCOMP (pp->rx, s, flags);
	  if (retval != 0)
	  {
	    char msgbuf[STRING];
	    regerror (retval, pp->rx, msgbuf, sizeof (msgbuf));
	    fprintf (stderr, _("restore_default(%s): error in regexp: %s\n"),
		     p->option, pp->pattern);
	    fprintf (stderr, "%s\n", msgbuf);
	    mutt_sleep (0);
	    FREE (&pp->pattern);
	    FREE (&pp->rx);
	  }
	}
      }
      break;
  }

  if (p->flags & R_INDEX)
    mutt_set_menu_redraw_full (MENU_MAIN);
  if (p->flags & R_PAGER)
    mutt_set_menu_redraw_full (MENU_PAGER);
  if (p->flags & R_RESORT_SUB)
    set_option (OPTSORTSUBTHREADS);
  if (p->flags & R_RESORT)
    set_option (OPTNEEDRESORT);
  if (p->flags & R_RESORT_INIT)
    set_option (OPTRESORTINIT);
  if (p->flags & R_TREE)
    set_option (OPTREDRAWTREE);
  if (p->flags & R_REFLOW)
    mutt_reflow_windows ();
#ifdef USE_SIDEBAR
  if (p->flags & R_SIDEBAR)
    mutt_set_current_menu_redraw (REDRAW_SIDEBAR);
#endif
  if (p->flags & R_MENU)
    mutt_set_current_menu_redraw_full ();
}

static size_t escape_string (char *dst, size_t len, const char* src)
{
  char* p = dst;

  if (!len)
    return 0;
  len--; /* save room for \0 */
#define ESC_CHAR(C)	do { *p++ = '\\'; if (p - dst < len) *p++ = C; } while(0)
  while (p - dst < len && src && *src)
  {
    switch (*src)
    {
    case '\n':
      ESC_CHAR('n');
      break;
    case '\r':
      ESC_CHAR('r');
      break;
    case '\t':
      ESC_CHAR('t');
      break;
    default:
      if ((*src == '\\' || *src == '"') && p - dst < len - 1)
	*p++ = '\\';
      *p++ = *src;
    }
    src++;
  }
#undef ESC_CHAR
  *p = '\0';
  return p - dst;
}

static void pretty_var (char *dst, size_t len, const char *option, const char *val)
{
  char *p = NULL;

  if (!len)
    return;

  strfcpy (dst, option, len);
  len--; /* save room for \0 */
  p = dst + mutt_strlen (dst);

  if (p - dst < len)
    *p++ = '=';
  if (p - dst < len)
    *p++ = '"';
  p += escape_string (p, len - (p - dst) + 1, val);	/* \0 terminate it */
  if (p - dst < len)
    *p++ = '"';
  *p = 0;
}

static int check_charset (struct option_t *opt, const char *val)
{
  char *p = NULL, *q = NULL, *s = safe_strdup (val);
  int rc = 0, strict = (strcmp (opt->option, "send_charset") == 0);

  if (!s)
    return rc;

  for (p = strtok_r (s, ":", &q); p; p = strtok_r (NULL, ":", &q))
  {
    if (!*p)
      continue;
    if (mutt_check_charset (p, strict) < 0)
    {
      rc = -1;
      break;
    }
  }

  FREE(&s);
  return rc;
}

static bool valid_show_multipart_alternative(const char *val)
{
  return ((mutt_strcmp(val, "inline") == 0) ||
          (mutt_strcmp(val, "info") == 0) ||
          (val == NULL) || (*val == 0));
}

char **mutt_envlist (void)
{
  return envlist;
}

/* Helper function for parse_setenv().
 * It's broken out because some other parts of mutt (filter.c) need
 * to set/overwrite environment variables in envlist before execing.
 */
void mutt_envlist_set (const char *name, const char *value)
{
  char **envp = envlist;
  char work[LONG_STRING];
  int count, len;

  len = mutt_strlen (name);

  /* Look for current slot to overwrite */
  count = 0;
  while (envp && *envp)
  {
    if ((mutt_strncmp (name, *envp, len) == 0) && (*envp)[len] == '=')
      break;
    envp++;
    count++;
  }

  /* Format var=value string */
  snprintf (work, sizeof(work), "%s=%s", NONULL (name), NONULL (value));

  /* If slot found, overwrite */
  if (envp && *envp)
    mutt_str_replace (envp, work);

  /* If not found, add new slot */
  else
  {
    safe_realloc (&envlist, sizeof(char *) * (count + 2));
    envlist[count] = safe_strdup (work);
    envlist[count + 1] = NULL;
  }
}

static int parse_setenv(BUFFER *tmp, BUFFER *s, unsigned long data, BUFFER *err)
{
  int query, unset, len;
  char *name = NULL, **save = NULL, **envp = envlist;
  int count = 0;

  query = 0;
  unset = data & MUTT_SET_UNSET;

  if (!MoreArgs (s))
  {
    strfcpy (err->data, _("too few arguments"), err->dsize);
    return -1;
  }

  if (*s->dptr == '?')
  {
    query = 1;
    s->dptr++;
  }

  /* get variable name */
  mutt_extract_token (tmp, s, MUTT_TOKEN_EQUAL);
  len = strlen (tmp->data);

  if (query)
  {
    int found = 0;
    while (envp && *envp)
    {
      if (mutt_strncmp (tmp->data, *envp, len) == 0)
      {
        if (!found)
        {
          mutt_endwin (NULL);
          found = 1;
        }
        puts (*envp);
      }
      envp++;
    }

    if (found)
    {
      mutt_any_key_to_continue (NULL);
      return 0;
    }

    snprintf (err->data, err->dsize, _("%s is unset"), tmp->data);
    return -1;
  }

  if (unset)
  {
    count = 0;
    while (envp && *envp)
    {
      if ((mutt_strncmp (tmp->data, *envp, len) == 0) && (*envp)[len] == '=')
      {
        /* shuffle down */
        save = envp++;
        while (*envp)
        {
          *save++ = *envp++;
          count++;
        }
        *save = NULL;
        safe_realloc (&envlist, sizeof(char *) * (count+1));
        return 0;
      }
      envp++;
      count++;
    }
    return -1;
  }

  /* set variable */

  if (*s->dptr == '=')
  {
    s->dptr++;
    SKIPWS (s->dptr);
  }

  if (!MoreArgs (s))
  {
    strfcpy (err->data, _("too few arguments"), err->dsize);
    return -1;
  }

  name = safe_strdup (tmp->data);
  mutt_extract_token (tmp, s, 0);
  mutt_envlist_set (name, tmp->data);
  FREE (&name);

  return 0;
}

static int parse_set (BUFFER *tmp, BUFFER *s, unsigned long data, BUFFER *err)
{
  int query, unset, inv, reset, r = 0;
  int idx = -1;
  const char *p = NULL;
  char scratch[_POSIX_PATH_MAX];
  char* myvar = NULL;

  while (MoreArgs (s))
  {
    /* reset state variables */
    query = 0;
    unset = data & MUTT_SET_UNSET;
    inv = data & MUTT_SET_INV;
    reset = data & MUTT_SET_RESET;
    myvar = NULL;

    if (*s->dptr == '?')
    {
      query = 1;
      s->dptr++;
    }
    else if (mutt_strncmp ("no", s->dptr, 2) == 0)
    {
      s->dptr += 2;
      unset = !unset;
    }
    else if (mutt_strncmp ("inv", s->dptr, 3) == 0)
    {
      s->dptr += 3;
      inv = !inv;
    }
    else if (*s->dptr == '&')
    {
      reset = 1;
      s->dptr++;
    }

    /* get the variable name */
    mutt_extract_token (tmp, s, MUTT_TOKEN_EQUAL);

    if (mutt_strncmp ("my_", tmp->data, 3) == 0)
      myvar = tmp->data;
    else if ((idx = mutt_option_index (tmp->data)) == -1 &&
	!(reset && (mutt_strcmp ("all", tmp->data) == 0)))
    {
      snprintf (err->data, err->dsize, _("%s: unknown variable"), tmp->data);
      return -1;
    }
    SKIPWS (s->dptr);

    if (reset)
    {
      if (query || unset || inv)
      {
	snprintf (err->data, err->dsize, _("prefix is illegal with reset"));
	return -1;
      }

      if (s && *s->dptr == '=')
      {
	snprintf (err->data, err->dsize, _("value is illegal with reset"));
	return -1;
      }

      if (mutt_strcmp ("all", tmp->data) == 0)
      {
	if (CurrentMenu == MENU_PAGER)
	{
	  snprintf (err->data, err->dsize, _("Not available in this menu."));
	  return -1;
	}
	for (idx = 0; MuttVars[idx].option; idx++)
	  restore_default (&MuttVars[idx]);
	mutt_set_current_menu_redraw_full ();
	set_option (OPTSORTSUBTHREADS);
	set_option (OPTNEEDRESORT);
	set_option (OPTRESORTINIT);
	set_option (OPTREDRAWTREE);
	return 0;
      }
      else
      {
	CHECK_PAGER;
        if (myvar)
          myvar_del (myvar);
        else
          restore_default (&MuttVars[idx]);
      }
    }
    else if (!myvar && DTYPE (MuttVars[idx].type) == DT_BOOL)
    {
      if (s && *s->dptr == '=')
      {
	if (unset || inv || query)
	{
	  snprintf (err->data, err->dsize, _("Usage: set variable=yes|no"));
	  return -1;
	}

	s->dptr++;
	mutt_extract_token (tmp, s, 0);
	if (ascii_strcasecmp ("yes", tmp->data) == 0)
	  unset = inv = 0;
	else if (ascii_strcasecmp ("no", tmp->data) == 0)
	  unset = 1;
	else
	{
	  snprintf (err->data, err->dsize, _("Usage: set variable=yes|no"));
	  return -1;
	}
      }

      if (query)
      {
	snprintf (err->data, err->dsize, option (MuttVars[idx].data)
			? _("%s is set") : _("%s is unset"), tmp->data);
	return 0;
      }

      CHECK_PAGER;
      if (unset)
	unset_option (MuttVars[idx].data);
      else if (inv)
	toggle_option (MuttVars[idx].data);
      else
	set_option (MuttVars[idx].data);
    }
    else if (myvar || DTYPE (MuttVars[idx].type) == DT_STR ||
	     DTYPE (MuttVars[idx].type) == DT_PATH ||
	     DTYPE (MuttVars[idx].type) == DT_ADDR ||
	     DTYPE (MuttVars[idx].type) == DT_MBCHARTBL)
    {
      if (unset)
      {
	CHECK_PAGER;
        if (myvar)
          myvar_del (myvar);
	else if (DTYPE (MuttVars[idx].type) == DT_ADDR)
	  rfc822_free_address ((ADDRESS **) MuttVars[idx].data);
	else if (DTYPE (MuttVars[idx].type) == DT_MBCHARTBL)
          free_mbchar_table ((mbchar_table **) MuttVars[idx].data);
	else
	  /* MuttVars[idx].data is already 'char**' (or some 'void**') or...
	   * so cast to 'void*' is okay */
	  FREE ((void *) MuttVars[idx].data);		/* __FREE_CHECKED__ */
      }
      else if (query || *s->dptr != '=')
      {
	char _tmp[LONG_STRING];
	const char *val = NULL;

        if (myvar)
        {
          if ((val = myvar_get (myvar)))
          {
	    pretty_var (err->data, err->dsize, myvar, val);
            break;
          }
          else
          {
            snprintf (err->data, err->dsize, _("%s: unknown variable"), myvar);
            return -1;
          }
        }
	else if (DTYPE (MuttVars[idx].type) == DT_ADDR)
	{
	  _tmp[0] = '\0';
	  rfc822_write_address (_tmp, sizeof (_tmp), *((ADDRESS **) MuttVars[idx].data), 0);
	  val = _tmp;
	}
	else if (DTYPE (MuttVars[idx].type) == DT_PATH)
	{
	  _tmp[0] = '\0';
	  strfcpy (_tmp, NONULL(*((char **) MuttVars[idx].data)), sizeof (_tmp));
	  mutt_pretty_mailbox (_tmp, sizeof (_tmp));
	  val = _tmp;
	}
	else if (DTYPE (MuttVars[idx].type) == DT_MBCHARTBL)
        {
          mbchar_table *mbt = (*((mbchar_table **) MuttVars[idx].data));
          val = mbt ? NONULL (mbt->orig_str) : "";
        }
	else
	  val = *((char **) MuttVars[idx].data);

	/* user requested the value of this variable */
	pretty_var (err->data, err->dsize, MuttVars[idx].option, NONULL(val));
	break;
      }
      else
      {
	CHECK_PAGER;
        s->dptr++;

        if (myvar)
	{
	  /* myvar is a pointer to tmp and will be lost on extract_token */
	  myvar = safe_strdup (myvar);
          myvar_del (myvar);
	}

        mutt_extract_token (tmp, s, 0);

        if (myvar)
        {
          myvar_set (myvar, tmp->data);
          FREE (&myvar);
	  myvar="don't resort";
        }
        else if (DTYPE (MuttVars[idx].type) == DT_PATH)
        {
	  /* MuttVars[idx].data is already 'char**' (or some 'void**') or...
	   * so cast to 'void*' is okay */
	  FREE ((void *) MuttVars[idx].data);		/* __FREE_CHECKED__ */

	  strfcpy (scratch, tmp->data, sizeof (scratch));
	  mutt_expand_path (scratch, sizeof (scratch));
	  *((char **) MuttVars[idx].data) = safe_strdup (scratch);
        }
        else if (DTYPE (MuttVars[idx].type) == DT_STR)
        {
	  if ((strstr (MuttVars[idx].option, "charset") &&
	       check_charset (&MuttVars[idx], tmp->data) < 0) |
	      /* $charset can't be empty, others can */
	      ((strcmp(MuttVars[idx].option, "charset") == 0) && ! *tmp->data))
	  {
	    snprintf (err->data, err->dsize, _("Invalid value for option %s: \"%s\""),
		      MuttVars[idx].option, tmp->data);
	    return -1;
	  }

	  FREE ((void *) MuttVars[idx].data);		/* __FREE_CHECKED__ */
	  *((char **) MuttVars[idx].data) = safe_strdup (tmp->data);
	  if (mutt_strcmp (MuttVars[idx].option, "charset") == 0)
	    mutt_set_charset (Charset);

          if ((mutt_strcmp (MuttVars[idx].option, "show_multipart_alternative") == 0) &&
              !valid_show_multipart_alternative(tmp->data))
          {
            snprintf (err->data, err->dsize, _("Invalid value for option %s: \"%s\""),
                      MuttVars[idx].option, tmp->data);
            return -1;
          }
        }
        else if (DTYPE (MuttVars[idx].type) == DT_MBCHARTBL)
        {
          free_mbchar_table ((mbchar_table **) MuttVars[idx].data);
          *((mbchar_table **) MuttVars[idx].data) = parse_mbchar_table (tmp->data);
        }
        else
        {
	  rfc822_free_address ((ADDRESS **) MuttVars[idx].data);
	  *((ADDRESS **) MuttVars[idx].data) = rfc822_parse_adrlist (NULL, tmp->data);
        }
      }
    }
    else if (DTYPE(MuttVars[idx].type) == DT_RX)
    {
      if (query || *s->dptr != '=')
      {
	/* user requested the value of this variable */
        REGEXP *ptr = (REGEXP *) MuttVars[idx].data;
	pretty_var (err->data, err->dsize, MuttVars[idx].option, NONULL(ptr->pattern));
	break;
      }

      if (option(OPTATTACHMSG) && (mutt_strcmp(MuttVars[idx].option, "reply_regexp") == 0))
      {
	snprintf (err->data, err->dsize, "Operation not permitted when in attach-message mode.");
	r = -1;
	break;
      }

      CHECK_PAGER;
      s->dptr++;

      /* copy the value of the string */
      mutt_extract_token (tmp, s, 0);

      if (parse_regex(idx, tmp, err))
	/* $reply_regexp and $alternates require special treatment */
	if (Context && Context->msgcount &&
	    (mutt_strcmp (MuttVars[idx].option, "reply_regexp") == 0))
	{
	  regmatch_t pmatch[1];
	  int i;

#define CUR_ENV Context->hdrs[i]->env
	  for (i = 0; i < Context->msgcount; i++)
	  {
	    if (CUR_ENV && CUR_ENV->subject)
	    {
	      CUR_ENV->real_subj = (regexec (ReplyRegexp.rx,
				    CUR_ENV->subject, 1, pmatch, 0)) ?
				    CUR_ENV->subject :
				    CUR_ENV->subject + pmatch[0].rm_eo;
	    }
	  }
#undef CUR_ENV
	}
    }
    else if (DTYPE(MuttVars[idx].type) == DT_MAGIC)
    {
      if (query || *s->dptr != '=')
      {
	switch (DefaultMagic)
	{
	  case MUTT_MBOX:
	    p = "mbox";
	    break;
	  case MUTT_MMDF:
	    p = "MMDF";
	    break;
	  case MUTT_MH:
	    p = "MH";
	    break;
	  case MUTT_MAILDIR:
	    p = "Maildir";
	    break;
	  default:
	    p = "unknown";
	    break;
	}
	snprintf (err->data, err->dsize, "%s=%s", MuttVars[idx].option, p);
	break;
      }

      CHECK_PAGER;
      s->dptr++;

      /* copy the value of the string */
      mutt_extract_token (tmp, s, 0);
      if (mx_set_magic (tmp->data))
      {
	snprintf (err->data, err->dsize, _("%s: invalid mailbox type"), tmp->data);
	r = -1;
	break;
      }
    }
    else if (DTYPE(MuttVars[idx].type) == DT_NUM)
    {
      short *ptr = (short *) MuttVars[idx].data;
      short val;
      int rc;

      if (query || *s->dptr != '=')
      {
	val = *ptr;
	/* compatibility alias */
	if (mutt_strcmp (MuttVars[idx].option, "wrapmargin") == 0)
	  val = *ptr < 0 ? -*ptr : 0;

	/* user requested the value of this variable */
	snprintf (err->data, err->dsize, "%s=%d", MuttVars[idx].option, val);
	break;
      }

      CHECK_PAGER;
      s->dptr++;

      mutt_extract_token (tmp, s, 0);
      rc = mutt_atos (tmp->data, (short *) &val);

      if (rc < 0 || !*tmp->data)
      {
	snprintf (err->data, err->dsize, _("%s: invalid value (%s)"), tmp->data,
		  rc == -1 ? _("format error") : _("number overflow"));
	r = -1;
	break;
      }
      else
	*ptr = val;

      /* these ones need a sanity check */
      if (mutt_strcmp (MuttVars[idx].option, "history") == 0)
      {
	if (*ptr < 0)
	  *ptr = 0;
	mutt_init_history ();
      }
      else if (mutt_strcmp (MuttVars[idx].option, "pager_index_lines") == 0)
      {
	if (*ptr < 0)
	  *ptr = 0;
      }
      else if (mutt_strcmp (MuttVars[idx].option, "wrapmargin") == 0)
      {
	if (*ptr < 0)
	  *ptr = 0;
	else
	  *ptr = -*ptr;
      }
#ifdef USE_IMAP
      else if (mutt_strcmp (MuttVars[idx].option, "imap_pipeline_depth") == 0)
      {
        if (*ptr < 0)
          *ptr = 0;
      }
#endif
    }
    else if (DTYPE (MuttVars[idx].type) == DT_QUAD)
    {
      if (query)
      {
	static const char * const vals[] = { "no", "yes", "ask-no", "ask-yes" };

	snprintf (err->data, err->dsize, "%s=%s", MuttVars[idx].option,
		  vals [ quadoption (MuttVars[idx].data) ]);
	break;
      }

      CHECK_PAGER;
      if (*s->dptr == '=')
      {
	s->dptr++;
	mutt_extract_token (tmp, s, 0);
	if (ascii_strcasecmp ("yes", tmp->data) == 0)
	  set_quadoption (MuttVars[idx].data, MUTT_YES);
	else if (ascii_strcasecmp ("no", tmp->data) == 0)
	  set_quadoption (MuttVars[idx].data, MUTT_NO);
	else if (ascii_strcasecmp ("ask-yes", tmp->data) == 0)
	  set_quadoption (MuttVars[idx].data, MUTT_ASKYES);
	else if (ascii_strcasecmp ("ask-no", tmp->data) == 0)
	  set_quadoption (MuttVars[idx].data, MUTT_ASKNO);
	else
	{
	  snprintf (err->data, err->dsize, _("%s: invalid value"), tmp->data);
	  r = -1;
	  break;
	}
      }
      else
      {
	if (inv)
	  toggle_quadoption (MuttVars[idx].data);
	else if (unset)
	  set_quadoption (MuttVars[idx].data, MUTT_NO);
	else
	  set_quadoption (MuttVars[idx].data, MUTT_YES);
      }
    }
    else if (DTYPE (MuttVars[idx].type) == DT_SORT)
    {
      const struct mapping_t *map = NULL;

      switch (MuttVars[idx].type & DT_SUBTYPE_MASK)
      {
	case DT_SORT_ALIAS:
	  map = SortAliasMethods;
	  break;
	case DT_SORT_BROWSER:
	  map = SortBrowserMethods;
	  break;
	case DT_SORT_KEYS:
          if ((WithCrypto & APPLICATION_PGP))
            map = SortKeyMethods;
	  break;
	case DT_SORT_AUX:
	  map = SortAuxMethods;
	  break;
	case DT_SORT_SIDEBAR:
	  map = SortSidebarMethods;
	  break;
	default:
	  map = SortMethods;
	  break;
      }

      if (!map)
      {
	snprintf (err->data, err->dsize, _("%s: Unknown type."), MuttVars[idx].option);
	r = -1;
	break;
      }

      if (query || *s->dptr != '=')
      {
	p = mutt_getnamebyvalue (*((short *) MuttVars[idx].data) & SORT_MASK, map);

	snprintf (err->data, err->dsize, "%s=%s%s%s", MuttVars[idx].option,
		  (*((short *) MuttVars[idx].data) & SORT_REVERSE) ? "reverse-" : "",
		  (*((short *) MuttVars[idx].data) & SORT_LAST) ? "last-" : "",
		  p);
	return 0;
      }
      CHECK_PAGER;
      s->dptr++;
      mutt_extract_token (tmp, s , 0);

      if (parse_sort ((short *) MuttVars[idx].data, tmp->data, map, err) == -1)
      {
	r = -1;
	break;
      }
    }
#ifdef USE_HCACHE
    else if (DTYPE (MuttVars[idx].type) == DT_HCACHE)
    {
      if (query || (*s->dptr != '='))
      {
        pretty_var (err->data, err->dsize, MuttVars[idx].option,
                NONULL ((*(char **)MuttVars[idx].data)));
	break;
      }

      CHECK_PAGER;
      s->dptr++;

      /* copy the value of the string */
      mutt_extract_token (tmp, s, 0);
      if (mutt_hcache_is_valid_backend(tmp->data))
      {
          FREE ((void *)MuttVars[idx].data); /* __FREE_CHECKED__ */
          *(char **)(MuttVars[idx].data) = safe_strdup(tmp->data);
      }
      else
      {
          snprintf (err->data, err->dsize, _("%s: invalid backend"), tmp->data);
          r = -1;
          break;
      }
    }
#endif
    else
    {
      snprintf (err->data, err->dsize, _("%s: unknown type"), MuttVars[idx].option);
      r = -1;
      break;
    }

    if (!myvar)
    {
      if (MuttVars[idx].flags & R_INDEX)
        mutt_set_menu_redraw_full (MENU_MAIN);
      if (MuttVars[idx].flags & R_PAGER)
        mutt_set_menu_redraw_full (MENU_PAGER);
      if (MuttVars[idx].flags & R_RESORT_SUB)
        set_option (OPTSORTSUBTHREADS);
      if (MuttVars[idx].flags & R_RESORT)
        set_option (OPTNEEDRESORT);
      if (MuttVars[idx].flags & R_RESORT_INIT)
        set_option (OPTRESORTINIT);
      if (MuttVars[idx].flags & R_TREE)
        set_option (OPTREDRAWTREE);
      if (MuttVars[idx].flags & R_REFLOW)
        mutt_reflow_windows ();
#ifdef USE_SIDEBAR
      if (MuttVars[idx].flags & R_SIDEBAR)
        mutt_set_current_menu_redraw (REDRAW_SIDEBAR);
#endif
      if (MuttVars[idx].flags & R_MENU)
        mutt_set_current_menu_redraw_full ();
    }
  }
  return r;
}

/* Stack structure
 * FILO designed to contain the list of config files that have been sourced
 * and avoid cyclic sourcing */
static LIST *MuttrcStack;

/* Use POSIX functions to convert a path to absolute, relatively to another path
 * Args:
 *  - path: instance containing the relative path to the file we want the absolute
 *     path of. Should be at least of PATH_MAX length, will contain the full result.
 *  - reference: path to a file which directory will be set as reference for setting
 *      up the absolute path.
 * Returns: true (1) on success, false (0) otherwise.
 */
static int to_absolute_path(char *path, const char *reference)
{
  char *ref_tmp = NULL, *dirpath = NULL;
  char abs_path[PATH_MAX];
  int path_len;

  /* if path is already absolute, don't do anything */
  if ((strlen(path) > 1) && (path[0] == '/'))
  {
    return true;
  }

  ref_tmp = safe_strdup(reference);
  dirpath = dirname(ref_tmp); /* get directory name of */
  strncpy(abs_path, dirpath, PATH_MAX);
  safe_strncat(abs_path, sizeof(abs_path), "/", 1); /* append a / at the end of the path */

  FREE(&ref_tmp);
  path_len = PATH_MAX - strlen(path);

  safe_strncat(abs_path, sizeof(abs_path), path, path_len > 0 ? path_len : 0);

  path = realpath(abs_path, path);

  if (!path)
  {
    printf("Error: issue converting path to absolute (%s)", strerror(errno));
    return false;
  }

  return true;
}

#define MAXERRS 128

/* reads the specified initialization file.  returns -1 if errors were found
   so that we can pause to let the user know...  */
static int source_rc (const char *rcfile_path, BUFFER *err)
{
  FILE *f = NULL;
  int line = 0, rc = 0, conv = 0, line_rc;
  BUFFER token;
  char *linebuf = NULL;
  char *currentline = NULL;
  char rcfile[PATH_MAX];
  size_t buflen;
  size_t rcfilelen;

  pid_t pid;

  strncpy(rcfile, rcfile_path, PATH_MAX);

  rcfilelen = mutt_strlen(rcfile);

  if (rcfile[rcfilelen-1] != '|')
  {
      if (!to_absolute_path(rcfile, mutt_front_list(MuttrcStack)))
      {
        mutt_error("Error: impossible to build path of '%s'.", rcfile_path);
        return -1;
      }

      if (!MuttrcStack || mutt_find_list(MuttrcStack, rcfile) == NULL)
      {
        mutt_push_list(&MuttrcStack, rcfile);
      }
      else
      {
        mutt_error("Error: Cyclic sourcing of configuration file '%s'.", rcfile);
        return -1;
      }
  }

  mutt_debug (2, "Reading configuration file '%s'.\n", rcfile);

  if ((f = mutt_open_read (rcfile, &pid)) == NULL)
  {
    snprintf (err->data, err->dsize, "%s: %s", rcfile, strerror (errno));
    return -1;
  }

  mutt_buffer_init (&token);
  while ((linebuf = mutt_read_line (linebuf, &buflen, f, &line, MUTT_CONT)) != NULL)
  {
    conv=ConfigCharset && (*ConfigCharset) && Charset;
    if (conv)
    {
      currentline=safe_strdup(linebuf);
      if (!currentline) continue;
      mutt_convert_string(&currentline, ConfigCharset, Charset, 0);
    }
    else
      currentline=linebuf;

    line_rc = mutt_parse_rc_line (currentline, &token, err);
    if (line_rc == -1) {
      mutt_error (_("Error in %s, line %d: %s"), rcfile, line, err->data);
      if (--rc < -MAXERRS)
      {
        if (conv) FREE(&currentline);
        break;
      }
    } else if (line_rc == 1) {
      break;	/* Found "finish" command */
    } else {
      if (rc < 0)
        rc = -1;
    }
    if (conv)
      FREE(&currentline);
  }
  FREE (&token.data);
  FREE (&linebuf);
  safe_fclose (&f);
  if (pid != -1)
    mutt_wait_filter (pid);
  if (rc)
  {
    /* the muttrc source keyword */
    snprintf (err->data, err->dsize, rc >= -MAXERRS ? _("source: errors in %s")
      : _("source: reading aborted due to too many errors in %s"), rcfile);
    rc = -1;
  }

  mutt_pop_list(&MuttrcStack);

  return rc;
}

#undef MAXERRS

static int parse_source (BUFFER *tmp, BUFFER *s, unsigned long data, BUFFER *err)
{
  char path[_POSIX_PATH_MAX];

  if (mutt_extract_token (tmp, s, 0) != 0)
  {
    snprintf (err->data, err->dsize, _("source: error at %s"), s->dptr);
    return -1;
  }
  if (MoreArgs (s))
  {
    strfcpy (err->data, _("source: too many arguments"), err->dsize);
    return -1;
  }
  strfcpy (path, tmp->data, sizeof (path));
  mutt_expand_path (path, sizeof (path));
  return source_rc (path, err);
}

/* line		command to execute

   token	scratch buffer to be used by parser.  caller should free
   		token->data when finished.  the reason for this variable is
		to avoid having to allocate and deallocate a lot of memory
		if we are parsing many lines.  the caller can pass in the
		memory to use, which avoids having to create new space for
		every call to this function.

   err		where to write error messages */
int mutt_parse_rc_line (/* const */ char *line, BUFFER *token, BUFFER *err)
{
  int i, r = 0;
  BUFFER expn;

  if (!line || !*line)
    return 0;

  mutt_buffer_init (&expn);
  expn.data = expn.dptr = line;
  expn.dsize = mutt_strlen (line);

  *err->data = 0;

  SKIPWS (expn.dptr);
  while (*expn.dptr)
  {
    if (*expn.dptr == '#')
      break; /* rest of line is a comment */
    if (*expn.dptr == ';')
    {
      expn.dptr++;
      continue;
    }
    mutt_extract_token (token, &expn, 0);
    for (i = 0; Commands[i].name; i++)
    {
      if (mutt_strcmp (token->data, Commands[i].name) == 0)
      {
        r = Commands[i].func (token, &expn, Commands[i].data, err);
        if (r != 0) {   /* -1 Error, +1 Finish */
          goto finish;  /* Propagate return code */
        }
        break;          /* Continue with next command */
      }
    }
    if (!Commands[i].name)
    {
      snprintf (err->data, err->dsize, _("%s: unknown command"), NONULL (token->data));
      r = -1;
      break;            /* Ignore the rest of the line */
    }
  }
finish:
  if (expn.destroy)
    FREE (&expn.data);
  return r;
}


#define NUMVARS (sizeof (MuttVars)/sizeof (MuttVars[0]))
#define NUMCOMMANDS (sizeof (Commands)/sizeof (Commands[0]))
/* initial string that starts completion. No telling how much crap
 * the user has typed so far. Allocate LONG_STRING just to be sure! */
static char User_typed [LONG_STRING] = {0};

static int  Num_matched = 0; /* Number of matches for completion */
static char Completed [STRING] = {0}; /* completed string (command or variable) */
static const char **Matches;
/* this is a lie until mutt_init runs: */
static int  Matches_listsize = MAX(NUMVARS,NUMCOMMANDS) + 10;

static void matches_ensure_morespace(int current)
{
  int base_space, extra_space, space;

  if (current > Matches_listsize - 2)
  {
    base_space = MAX(NUMVARS,NUMCOMMANDS) + 1;
    extra_space = Matches_listsize - base_space;
    extra_space *= 2;
    space = base_space + extra_space;
    safe_realloc (&Matches, space * sizeof (char *));
    memset (&Matches[current + 1], 0, space - current);
    Matches_listsize = space;
  }
}

/* helper function for completion.  Changes the dest buffer if
   necessary/possible to aid completion.
	dest == completion result gets here.
	src == candidate for completion.
	try == user entered data for completion.
	len == length of dest buffer.
*/
static void candidate (char *dest, char *try, const char *src, int len)
{
  if (!dest || !try || !src)
    return;

  int l;

  if (strstr (src, try) == src)
  {
    matches_ensure_morespace (Num_matched);
    Matches[Num_matched++] = src;
    if (dest[0] == 0)
      strfcpy (dest, src, len);
    else
    {
      for (l = 0; src[l] && src[l] == dest[l]; l++);
      dest[l] = 0;
    }
  }
}

#ifdef USE_LUA
const struct command_t *mutt_command_get(const char *s)
{
  for (int i = 0; Commands[i].name; i++)
    if (mutt_strcmp(s, Commands[i].name) == 0)
      return &Commands[i];
  return NULL;
}
#endif

void mutt_commands_apply(void *data,
                         void (*application)(void *, const struct command_t *))
{
  for (int i = 0; Commands[i].name; i++)
    application(data, &Commands[i]);
}

int mutt_command_complete (char *buffer, size_t len, int pos, int numtabs)
{
  char *pt = buffer;
  int num;
  int spaces; /* keep track of the number of leading spaces on the line */
  myvar_t *myv = NULL;

  SKIPWS (buffer);
  spaces = buffer - pt;

  pt = buffer + pos - spaces;
  while ((pt > buffer) && !isspace ((unsigned char) *pt))
    pt--;

  if (pt == buffer) /* complete cmd */
  {
    /* first TAB. Collect all the matches */
    if (numtabs == 1)
    {
      Num_matched = 0;
      strfcpy (User_typed, pt, sizeof (User_typed));
      memset (Matches, 0, Matches_listsize);
      memset (Completed, 0, sizeof (Completed));
      for (num = 0; Commands[num].name; num++)
	candidate (Completed, User_typed, Commands[num].name, sizeof (Completed));
      matches_ensure_morespace (Num_matched);
      Matches[Num_matched++] = User_typed;

      /* All matches are stored. Longest non-ambiguous string is ""
       * i.e. don't change 'buffer'. Fake successful return this time */
      if (User_typed[0] == 0)
	return 1;
    }

    if (Completed[0] == 0 && User_typed[0])
      return 0;

     /* Num_matched will _always_ be at least 1 since the initial
      * user-typed string is always stored */
    if (numtabs == 1 && Num_matched == 2)
      snprintf(Completed, sizeof(Completed),"%s", Matches[0]);
    else if (numtabs > 1 && Num_matched > 2)
      /* cycle thru all the matches */
      snprintf(Completed, sizeof(Completed), "%s",
	       Matches[(numtabs - 2) % Num_matched]);

    /* return the completed command */
    strncpy (buffer, Completed, len - spaces);
  }
  else if ((mutt_strncmp (buffer, "set", 3) == 0)
	   || (mutt_strncmp (buffer, "unset", 5) == 0)
	   || (mutt_strncmp (buffer, "reset", 5) == 0)
	   || (mutt_strncmp (buffer, "toggle", 6) == 0))
  { 		/* complete variables */
    static const char * const prefixes[] = { "no", "inv", "?", "&", 0 };

    pt++;
    /* loop through all the possible prefixes (no, inv, ...) */
    if (mutt_strncmp (buffer, "set", 3) == 0)
    {
      for (num = 0; prefixes[num]; num++)
      {
	if (mutt_strncmp (pt, prefixes[num], mutt_strlen (prefixes[num])) == 0)
	{
	  pt += mutt_strlen (prefixes[num]);
	  break;
	}
      }
    }

    /* first TAB. Collect all the matches */
    if (numtabs == 1)
    {
      Num_matched = 0;
      strfcpy (User_typed, pt, sizeof (User_typed));
      memset (Matches, 0, Matches_listsize);
      memset (Completed, 0, sizeof (Completed));
      for (num = 0; MuttVars[num].option; num++)
	candidate (Completed, User_typed, MuttVars[num].option, sizeof (Completed));
      for (myv = MyVars; myv; myv = myv->next)
	candidate (Completed, User_typed, myv->name, sizeof (Completed));
      matches_ensure_morespace (Num_matched);
      Matches[Num_matched++] = User_typed;

      /* All matches are stored. Longest non-ambiguous string is ""
       * i.e. don't change 'buffer'. Fake successful return this time */
      if (User_typed[0] == 0)
	return 1;
    }

    if (Completed[0] == 0 && User_typed[0])
      return 0;

    /* Num_matched will _always_ be at least 1 since the initial
     * user-typed string is always stored */
    if (numtabs == 1 && Num_matched == 2)
      snprintf(Completed, sizeof(Completed),"%s", Matches[0]);
    else if (numtabs > 1 && Num_matched > 2)
    /* cycle thru all the matches */
      snprintf(Completed, sizeof(Completed), "%s",
	       Matches[(numtabs - 2) % Num_matched]);

    strncpy (pt, Completed, buffer + len - pt - spaces);
  }
  else if (mutt_strncmp (buffer, "exec", 4) == 0)
  {
    const struct binding_t *menu = km_get_table (CurrentMenu);

    if (!menu && CurrentMenu != MENU_PAGER)
      menu = OpGeneric;

    pt++;
    /* first TAB. Collect all the matches */
    if (numtabs == 1)
    {
      Num_matched = 0;
      strfcpy (User_typed, pt, sizeof (User_typed));
      memset (Matches, 0, Matches_listsize);
      memset (Completed, 0, sizeof (Completed));
      for (num = 0; menu[num].name; num++)
	candidate (Completed, User_typed, menu[num].name, sizeof (Completed));
      /* try the generic menu */
      if (Completed[0] == 0 && CurrentMenu != MENU_PAGER)
      {
	menu = OpGeneric;
	for (num = 0; menu[num].name; num++)
	  candidate (Completed, User_typed, menu[num].name, sizeof (Completed));
      }
      matches_ensure_morespace (Num_matched);
      Matches[Num_matched++] = User_typed;

      /* All matches are stored. Longest non-ambiguous string is ""
       * i.e. don't change 'buffer'. Fake successful return this time */
      if (User_typed[0] == 0)
	return 1;
    }

    if (Completed[0] == 0 && User_typed[0])
      return 0;

    /* Num_matched will _always_ be at least 1 since the initial
     * user-typed string is always stored */
    if (numtabs == 1 && Num_matched == 2)
      snprintf(Completed, sizeof(Completed),"%s", Matches[0]);
    else if (numtabs > 1 && Num_matched > 2)
    /* cycle thru all the matches */
      snprintf(Completed, sizeof(Completed), "%s",
	       Matches[(numtabs - 2) % Num_matched]);

    strncpy (pt, Completed, buffer + len - pt - spaces);
  }
  else
    return 0;

  return 1;
}

int mutt_var_value_complete (char *buffer, size_t len, int pos)
{
  char var[STRING], *pt = buffer;
  int spaces;

  if (buffer[0] == 0)
    return 0;

  SKIPWS (buffer);
  spaces = buffer - pt;

  pt = buffer + pos - spaces;
  while ((pt > buffer) && !isspace ((unsigned char) *pt))
    pt--;
  pt++; /* move past the space */
  if (*pt == '=') /* abort if no var before the '=' */
    return 0;

  if (mutt_strncmp (buffer, "set", 3) == 0)
  {
    int idx;
    char val[LONG_STRING];
    const char *myvarval = NULL;

    strfcpy (var, pt, sizeof (var));
    /* ignore the trailing '=' when comparing */
    var[mutt_strlen (var) - 1] = 0;
    if ((idx = mutt_option_index (var)) == -1)
    {
      if ((myvarval = myvar_get(var)) != NULL)
      {
	pretty_var (pt, len - (pt - buffer), var, myvarval);
	return 1;
      }
      return 0; /* no such variable. */
    }
    else if (var_to_string (idx, val, sizeof (val)))
    {
      snprintf (pt, len - (pt - buffer), "%s=\"%s\"", var, val);
      return 1;
    }
  }
  return 0;
}

#ifdef USE_NOTMUCH

/* Fetch a list of all notmuch tags and insert them into the completion
 * machinery.
 */
static int complete_all_nm_tags (const char *pt)
{
  int num;
  int tag_count_1 = 0;
  int tag_count_2 = 0;

  Num_matched = 0;
  strfcpy (User_typed, pt, sizeof (User_typed));
  memset (Matches, 0, Matches_listsize);
  memset (Completed, 0, sizeof (Completed));

  nm_longrun_init(Context, false);

  /* Work out how many tags there are. */
  if (nm_get_all_tags(Context, NULL, &tag_count_1) || tag_count_1 == 0)
    goto done;

  /* Free the old list, if any. */
  if (nm_tags != NULL) {
    int i;
    for (i = 0; nm_tags[i] != NULL; i++)
      FREE (&nm_tags[i]);
    FREE (&nm_tags);
  }
  /* Allocate a new list, with sentinel. */
  nm_tags = safe_malloc((tag_count_1 + 1) * sizeof (char *));
  nm_tags[tag_count_1] = NULL;

  /* Get all the tags. */
  if (nm_get_all_tags(Context, nm_tags, &tag_count_2) ||
      tag_count_1 != tag_count_2) {
    FREE (&nm_tags);
    nm_tags = NULL;
    nm_longrun_done(Context);
    return -1;
  }

  /* Put them into the completion machinery. */
  for (num = 0; num < tag_count_1; num++) {
    candidate (Completed, User_typed, nm_tags[num], sizeof (Completed));
  }

  matches_ensure_morespace (Num_matched);
  Matches[Num_matched++] = User_typed;

done:
  nm_longrun_done(Context);
  return 0;
}

/* Return the last instance of needle in the haystack, or NULL.
 * Like strstr(), only backwards, and for a limited haystack length.
 */
static const char* rstrnstr(const char* haystack,
                            size_t haystack_length,
                            const char* needle)
{
  int needle_length = strlen(needle);
  const char* haystack_end = haystack + haystack_length - needle_length;
  const char* p = NULL;

  for (p = haystack_end; p >= haystack; --p)
  {
    size_t i;
    for (i = 0; i < needle_length; ++i) {
      if (p[i] != needle[i])
        goto next;
    }
    return p;

    next:;
  }
  return NULL;
}

/* Complete the nearest "tag:"-prefixed string previous to pos. */
bool mutt_nm_query_complete (char *buffer, size_t len, int pos, int numtabs)
{
  char *pt = buffer;
  int spaces;

  SKIPWS (buffer);
  spaces = buffer - pt;

  pt = (char *)rstrnstr((char *)buffer, pos, "tag:");
  if (pt != NULL) {
    pt += 4;
    if (numtabs == 1) {
      /* First TAB. Collect all the matches */
      complete_all_nm_tags(pt);

      /* All matches are stored. Longest non-ambiguous string is ""
       * i.e. don't change 'buffer'. Fake successful return this time.
       */
      if (User_typed[0] == 0)
	return true;
    }

    if (Completed[0] == 0 && User_typed[0])
      return false;

    /* Num_matched will _always_ be at least 1 since the initial
     * user-typed string is always stored */
    if (numtabs == 1 && Num_matched == 2)
      snprintf(Completed, sizeof(Completed),"%s", Matches[0]);
    else if (numtabs > 1 && Num_matched > 2)
      /* cycle thru all the matches */
      snprintf(Completed, sizeof(Completed), "%s",
	       Matches[(numtabs - 2) % Num_matched]);

    /* return the completed query */
    strncpy (pt, Completed, buffer + len - pt - spaces);
  }
  else
    return false;

  return true;
}

/* Complete the nearest "+" or "-" -prefixed string previous to pos. */
bool mutt_nm_tag_complete (char *buffer, size_t len, int pos, int numtabs)
{
  if (!buffer)
    return false;

  char *pt = buffer;

  /* Only examine the last token */
  char *last_space = strrchr (buffer, ' ');
  if (last_space)
    pt = (last_space + 1);

  /* Skip the +/- */
  if ((pt[0] == '+') || (pt[0] == '-'))
    pt++;

  if (numtabs == 1)
  {
    /* First TAB. Collect all the matches */
    complete_all_nm_tags(pt);

    /* All matches are stored. Longest non-ambiguous string is ""
      * i.e. don't change 'buffer'. Fake successful return this time.
      */
    if (User_typed[0] == 0)
      return true;
  }

  if (Completed[0] == 0 && User_typed[0])
    return false;

  /* Num_matched will _always_ be at least 1 since the initial
    * user-typed string is always stored */
  if (numtabs == 1 && Num_matched == 2)
    snprintf(Completed, sizeof(Completed),"%s", Matches[0]);
  else if (numtabs > 1 && Num_matched > 2)
    /* cycle thru all the matches */
    snprintf(Completed, sizeof(Completed), "%s",
	      Matches[(numtabs - 2) % Num_matched]);

  /* return the completed query */
  strncpy (pt, Completed, buffer + len - pt);

  return true;
}
#endif

int var_to_string (int idx, char* val, size_t len)
{
  char tmp[LONG_STRING];
  static const char * const vals[] = { "no", "yes", "ask-no", "ask-yes" };

  tmp[0] = '\0';

  if ((DTYPE(MuttVars[idx].type) == DT_STR) ||
      (DTYPE(MuttVars[idx].type) == DT_PATH) ||
      (DTYPE(MuttVars[idx].type) == DT_RX))
  {
    strfcpy (tmp, NONULL (*((char **) MuttVars[idx].data)), sizeof (tmp));
    if (DTYPE (MuttVars[idx].type) == DT_PATH)
      mutt_pretty_mailbox (tmp, sizeof (tmp));
  }
  else if (DTYPE (MuttVars[idx].type) == DT_MBCHARTBL)
  {
    mbchar_table *mbt = (*((mbchar_table **) MuttVars[idx].data));
    strfcpy (tmp, mbt ? NONULL (mbt->orig_str) : "", sizeof (tmp));
  }
  else if (DTYPE (MuttVars[idx].type) == DT_ADDR)
  {
    rfc822_write_address (tmp, sizeof (tmp), *((ADDRESS **) MuttVars[idx].data), 0);
  }
  else if (DTYPE (MuttVars[idx].type) == DT_QUAD)
    strfcpy (tmp, vals[quadoption (MuttVars[idx].data)], sizeof (tmp));
  else if (DTYPE (MuttVars[idx].type) == DT_NUM)
  {
    short sval = *((short *) MuttVars[idx].data);

    /* avert your eyes, gentle reader */
    if (mutt_strcmp (MuttVars[idx].option, "wrapmargin") == 0)
      sval = sval > 0 ? 0 : -sval;

    snprintf (tmp, sizeof (tmp), "%d", sval);
  }
  else if (DTYPE (MuttVars[idx].type) == DT_SORT)
  {
    const struct mapping_t *map = NULL;
    const char *p = NULL;

    switch (MuttVars[idx].type & DT_SUBTYPE_MASK)
    {
      case DT_SORT_ALIAS:
        map = SortAliasMethods;
        break;
      case DT_SORT_BROWSER:
        map = SortBrowserMethods;
        break;
      case DT_SORT_KEYS:
        if ((WithCrypto & APPLICATION_PGP))
          map = SortKeyMethods;
        else
          map = SortMethods;
        break;
      default:
        map = SortMethods;
        break;
    }
    p = mutt_getnamebyvalue (*((short *) MuttVars[idx].data) & SORT_MASK, map);
    snprintf (tmp, sizeof (tmp), "%s%s%s",
              (*((short *) MuttVars[idx].data) & SORT_REVERSE) ? "reverse-" : "",
              (*((short *) MuttVars[idx].data) & SORT_LAST) ? "last-" : "",
              p);
  }
  else if (DTYPE (MuttVars[idx].type) == DT_MAGIC)
  {
    char *p = NULL;

    switch (DefaultMagic)
    {
      case MUTT_MBOX:
        p = "mbox";
        break;
      case MUTT_MMDF:
        p = "MMDF";
        break;
      case MUTT_MH:
        p = "MH";
        break;
      case MUTT_MAILDIR:
        p = "Maildir";
        break;
      default:
        p = "unknown";
    }
    strfcpy (tmp, p, sizeof (tmp));
  }
  else if (DTYPE (MuttVars[idx].type) == DT_BOOL)
    strfcpy (tmp, option (MuttVars[idx].data) ? "yes" : "no", sizeof (tmp));
  else
    return 0;

  escape_string (val, len - 1, tmp);

  return 1;
}

/* Implement the -Q command line flag */
int mutt_query_variables (LIST *queries)
{
  LIST *p = NULL;

  char command[STRING];

  BUFFER err, token;

  mutt_buffer_init (&err);
  mutt_buffer_init (&token);

  err.dsize = STRING;
  err.data = safe_malloc (err.dsize);

  for (p = queries; p; p = p->next)
  {
    snprintf (command, sizeof (command), "set ?%s\n", p->data);
    if (mutt_parse_rc_line (command, &token, &err) == -1)
    {
      fprintf (stderr, "%s\n", err.data);
      FREE (&token.data);
      FREE (&err.data);

      return 1;
    }
    printf ("%s\n", err.data);
  }

  FREE (&token.data);
  FREE (&err.data);

  return 0;
}

/* dump out the value of all the variables we have */
int mutt_dump_variables (int hide_sensitive)
{
  int i;

  char command[STRING];

  BUFFER err, token;

  mutt_buffer_init (&err);
  mutt_buffer_init (&token);

  err.dsize = STRING;
  err.data = safe_malloc (err.dsize);

  for (i = 0; MuttVars[i].option; i++)
  {
    if (MuttVars[i].type == DT_SYN)
      continue;

    if (hide_sensitive && IS_SENSITIVE(MuttVars[i]))
    {
        printf("%s='***'\n", MuttVars[i].option);
        continue;
    }
    snprintf (command, sizeof (command), "set ?%s\n", MuttVars[i].option);
    if (mutt_parse_rc_line (command, &token, &err) == -1)
    {
      fprintf (stderr, "%s\n", err.data);
      FREE (&token.data);
      FREE (&err.data);

      return 1;
    }
    printf("%s\n", err.data);
  }

  FREE (&token.data);
  FREE (&err.data);

  return 0;
}

const char *mutt_getnamebyvalue (int val, const struct mapping_t *map)
{
  int i;

  for (i=0; map[i].name; i++)
    if (map[i].value == val)
      return map[i].name;
  return NULL;
}

int mutt_getvaluebyname (const char *name, const struct mapping_t *map)
{
  int i;

  for (i = 0; map[i].name; i++)
    if (ascii_strcasecmp (map[i].name, name) == 0)
      return map[i].value;
  return -1;
}

#ifdef DEBUG
static void start_debug (void)
{
  int i;
  char buf[_POSIX_PATH_MAX];
  char buf2[_POSIX_PATH_MAX];

  /* rotate the old debug logs */
  for (i=3; i>=0; i--)
  {
    snprintf (buf, sizeof(buf), "%s/.muttdebug%d", NONULL(Homedir), i);
    snprintf (buf2, sizeof(buf2), "%s/.muttdebug%d", NONULL(Homedir), i+1);
    rename (buf, buf2);
  }
  if ((debugfile = safe_fopen(buf, "w")) != NULL)
  {
    setbuf (debugfile, NULL); /* don't buffer the debugging output! */
    mutt_debug (1, "NeoMutt %s%s (%s) debugging at level %d\n",
                PACKAGE_VERSION, GitVer, MUTT_VERSION, debuglevel);
  }
}
#endif

static int execute_commands (LIST *p)
{
  BUFFER err, token;

  mutt_buffer_init (&err);
  err.dsize = STRING;
  err.data = safe_malloc (err.dsize);
  mutt_buffer_init (&token);
  for (; p; p = p->next)
  {
    if (mutt_parse_rc_line (p->data, &token, &err) == -1)
    {
      fprintf (stderr, _("Error in command line: %s\n"), err.data);
      FREE (&token.data);
      FREE (&err.data);

      return -1;
    }
  }
  FREE (&token.data);
  FREE (&err.data);

  return 0;
}

static char* find_cfg (const char *home, const char *xdg_cfg_home)
{
  const char* names[] =
  {
    "neomuttrc-" PACKAGE_VERSION,
    "neomuttrc",
    "muttrc-" MUTT_VERSION,
    "muttrc",
    NULL,
  };

  const char* locations[][2] =
  {
    { xdg_cfg_home, "mutt/", },
    { home, ".", },
    { home, ".mutt/" },
    { NULL, NULL },
  };

  int i;

  for (i = 0; locations[i][0] || locations[i][1]; i++)
  {
    int j;

    if (!locations[i][0])
      continue;

    for (j = 0; names[j]; j++)
    {
      char buffer[STRING];

      snprintf (buffer, sizeof (buffer),
                "%s/%s%s", locations[i][0], locations[i][1], names[j]);
      if (access (buffer, F_OK) == 0)
        return safe_strdup(buffer);
    }
  }

  return NULL;
}

int getmailname(char *s, size_t l)
{
    FILE *f;
    char tmp[512];
    char *p = tmp;

    if ((f = fopen ("/etc/mailname", "r")) == NULL)
       return (-1);

    if (fgets (tmp, 510, f) != NULL) {
      while (*p && !ISSPACE(*p) && l > 0) {
	*s++ = *p++;
	l--;
      }
      if (*(s-1) == '.')
	s--;
      *s = 0;

      fclose (f);
      return 0;
    }
    fclose (f);
    return (-1);
}

void mutt_init (int skip_sys_rc, LIST *commands)
{
  struct passwd *pw = NULL;
  struct utsname utsname;
  char *p, buffer[STRING];
  int i, need_pause = 0;
  BUFFER err;

  mutt_buffer_init (&err);
  err.dsize = STRING;
  err.data = safe_malloc(err.dsize);
  err.dptr = err.data;

  Groups = hash_create (1031, 0);
  /* reverse alias keys need to be strdup'ed because of idna conversions */
  ReverseAlias = hash_create (1031, MUTT_HASH_STRCASECMP | MUTT_HASH_STRDUP_KEYS |
                              MUTT_HASH_ALLOW_DUPS);
#ifdef USE_NOTMUCH
  TagTransforms = hash_create (64, 1);
  TagFormats = hash_create (64, 0);
#endif

  mutt_menu_init ();

  snprintf (AttachmentMarker, sizeof (AttachmentMarker),
	    "\033]9;%" PRIu64 "\a", mutt_rand64());

  /* on one of the systems I use, getcwd() does not return the same prefix
     as is listed in the passwd file */
  if ((p = getenv ("HOME")))
    Homedir = safe_strdup (p);

  /* Get some information about the user */
  if ((pw = getpwuid (getuid ())))
  {
    char rnbuf[STRING];

    Username = safe_strdup (pw->pw_name);
    if (!Homedir)
      Homedir = safe_strdup (pw->pw_dir);

    Realname = safe_strdup (mutt_gecos_name (rnbuf, sizeof (rnbuf), pw));
    Shell = safe_strdup (pw->pw_shell);
    endpwent ();
  }
  else
  {
    if (!Homedir)
    {
      mutt_endwin (NULL);
      fputs (_("unable to determine home directory"), stderr);
      exit (1);
    }
    if ((p = getenv ("USER")))
      Username = safe_strdup (p);
    else
    {
      mutt_endwin (NULL);
      fputs (_("unable to determine username"), stderr);
      exit (1);
    }
    Shell = safe_strdup ((p = getenv ("SHELL")) ? p : "/bin/sh");
  }

#ifdef DEBUG
  /* Start up debugging mode if requested */
  if (debuglevel > 0)
    start_debug ();
#endif

  /* And about the host... */

  /*
   * The call to uname() shouldn't fail, but if it does, the system is horribly
   * broken, and the system's networking configuration is in an unreliable
   * state.  We should bail.
   */
  if ((uname (&utsname)) == -1)
  {
    mutt_endwin (NULL);
    perror (_("unable to determine nodename via uname()"));
    exit (1);
  }

  /* some systems report the FQDN instead of just the hostname */
  if ((p = strchr (utsname.nodename, '.')))
    Hostname = mutt_substrdup (utsname.nodename, p);
  else
    Hostname = safe_strdup (utsname.nodename);

  /* now get FQDN.  Use /etc/mailname first, then configured domain, DNS next, then uname */
  if (getmailname(buffer, sizeof (buffer)) != -1)
    Fqdn = safe_strdup(buffer);
#ifdef DOMAIN
  /* we have a compile-time domain name, use that for Fqdn */
  if (!Fqdn)
  {
    Fqdn = safe_malloc (mutt_strlen (DOMAIN) + mutt_strlen (Hostname) + 2);
    sprintf (Fqdn, "%s.%s", NONULL(Hostname), DOMAIN);	/* __SPRINTF_CHECKED__ */
  }
  else
#else
  if (!(getdnsdomainname (buffer, sizeof (buffer))))
  {
    Fqdn = safe_malloc (mutt_strlen (buffer) + mutt_strlen (Hostname) + 2);
    sprintf (Fqdn, "%s.%s", NONULL(Hostname), buffer);	/* __SPRINTF_CHECKED__ */
  }
  else
    /*
     * DNS failed, use the nodename.  Whether or not the nodename had a '.' in
     * it, we can use the nodename as the FQDN.  On hosts where DNS is not
     * being used, e.g. small network that relies on hosts files, a short host
     * name is all that is required for SMTP to work correctly.  It could be
     * wrong, but we've done the best we can, at this point the onus is on the
     * user to provide the correct hostname if the nodename won't work in their
     * network.
     */
    Fqdn = safe_strdup(utsname.nodename);
#endif

#ifdef USE_NNTP
  {
    FILE *f = NULL;
    char *c = NULL;

    if ((f = safe_fopen (SYSCONFDIR "/nntpserver", "r")))
    {
      buffer[0] = '\0';
      fgets (buffer, sizeof (buffer), f);
      p = buffer;
      SKIPWS (p);
      c = p;
      while (*c && (*c != ' ') && (*c != '\t') && (*c != '\r') && (*c != '\n')) c++;
      *c = '\0';
      NewsServer = safe_strdup (p);
      fclose (f);
    }
  }
  if ((p = getenv ("NNTPSERVER")))
    NewsServer = safe_strdup (p);
#endif

  if ((p = getenv ("MAIL")))
    Spoolfile = safe_strdup (p);
  else if ((p = getenv ("MAILDIR")))
    Spoolfile = safe_strdup (p);
  else
  {
#ifdef HOMESPOOL
    mutt_concat_path (buffer, NONULL (Homedir), MAILPATH, sizeof (buffer));
#else
    mutt_concat_path (buffer, MAILPATH, NONULL(Username), sizeof (buffer));
#endif
    Spoolfile = safe_strdup (buffer);
  }

  if ((p = getenv ("MAILCAPS")))
    MailcapPath = safe_strdup (p);
  else
  {
    /* Default search path from RFC1524 */
    MailcapPath = safe_strdup ("~/.mailcap:" PKGDATADIR "/mailcap:" SYSCONFDIR "/mailcap:/etc/mailcap:/usr/etc/mailcap:/usr/local/etc/mailcap");
  }

  Tempdir = safe_strdup ((p = getenv ("TMPDIR")) ? p : "/tmp");

  p = getenv ("VISUAL");
  if (!p)
  {
    p = getenv ("EDITOR");
    if (!p)
      p = "vi";
  }
  Editor = safe_strdup (p);
  Visual = safe_strdup (p);

  if ((p = getenv ("REPLYTO")) != NULL)
  {
    BUFFER buf, token;

    snprintf (buffer, sizeof (buffer), "Reply-To: %s", p);

    mutt_buffer_init (&buf);
    buf.data = buf.dptr = buffer;
    buf.dsize = mutt_strlen (buffer);

    mutt_buffer_init (&token);
    parse_my_hdr (&token, &buf, 0, &err);
    FREE (&token.data);
  }

  if ((p = getenv ("EMAIL")) != NULL)
    From = rfc822_parse_adrlist (NULL, p);

  mutt_set_langinfo_charset ();
  mutt_set_charset (Charset);

  Matches = safe_calloc (Matches_listsize, sizeof (char *));

  /* Set standard defaults */
  for (i = 0; MuttVars[i].option; i++)
  {
    set_default (&MuttVars[i]);
    restore_default (&MuttVars[i]);
  }

  CurrentMenu = MENU_MAIN;


#ifndef LOCALES_HACK
  /* Do we have a locale definition? */
  if (((p = getenv ("LC_ALL")) != NULL && p[0]) ||
      ((p = getenv ("LANG")) != NULL && p[0]) ||
      ((p = getenv ("LC_CTYPE")) != NULL && p[0]))
    set_option (OPTLOCALES);
#endif

#ifdef HAVE_GETSID
  /* Unset suspend by default if we're the session leader */
  if (getsid(0) == getpid())
    unset_option (OPTSUSPEND);
#endif

  mutt_init_history ();

  /* RFC2368, "4. Unsafe headers"
   * The creator of a mailto URL cannot expect the resolver of a URL to
   * understand more than the "subject" and "body" headers. Clients that
   * resolve mailto URLs into mail messages should be able to correctly
   * create RFC 822-compliant mail messages using the "subject" and "body"
   * headers.
   */
  add_to_list(&MailtoAllow, "body");
  add_to_list(&MailtoAllow, "subject");

  if (!Muttrc)
  {
    char *xdg_cfg_home = getenv ("XDG_CONFIG_HOME");

    if (!xdg_cfg_home && Homedir)
    {
      snprintf (buffer, sizeof (buffer), "%s/.config", Homedir);
      xdg_cfg_home = buffer;
    }

    char *config = find_cfg (Homedir, xdg_cfg_home);
    if (config)
    {
      Muttrc = mutt_add_list (Muttrc, config);
      FREE(&config);
    }
  }
  else
  {
    for (LIST *config = Muttrc; config != NULL; config = config->next)
    {
      strfcpy(buffer, config->data, sizeof(buffer));
      FREE(&config->data);
      mutt_expand_path(buffer, sizeof(buffer));
      config->data = safe_strdup(buffer);
      if (access(config->data, F_OK))
      {
        snprintf(buffer, sizeof(buffer), "%s: %s", config->data, strerror(errno));
        mutt_endwin(buffer);
        exit(1);
      }
    }
  }

  if (Muttrc && Muttrc->data)
  {
    FREE (&AliasFile);
    AliasFile = safe_strdup (Muttrc->data);
  }

  /* Process the global rc file if it exists and the user hasn't explicitly
     requested not to via "-n".  */
  if (!skip_sys_rc)
  {
    do
    {
      if (mutt_set_xdg_path (kXDGConfigDirs, buffer, sizeof (buffer)))
        break;

      snprintf (buffer, sizeof (buffer), "%s/neomuttrc-%s", SYSCONFDIR, PACKAGE_VERSION);
      if (access (buffer, F_OK) == 0)
        break;

      snprintf (buffer, sizeof (buffer), "%s/neomuttrc", SYSCONFDIR);
      if (access (buffer, F_OK) == 0)
        break;

      snprintf (buffer, sizeof (buffer), "%s/Muttrc-%s", SYSCONFDIR, MUTT_VERSION);
      if (access (buffer, F_OK) == 0)
        break;

      snprintf (buffer, sizeof (buffer), "%s/Muttrc", SYSCONFDIR);
      if (access (buffer, F_OK) == 0)
        break;

      snprintf (buffer, sizeof (buffer), "%s/neomuttrc-%s", PKGDATADIR, PACKAGE_VERSION);
      if (access (buffer, F_OK) == 0)
        break;

      snprintf (buffer, sizeof (buffer), "%s/neomuttrc", PKGDATADIR);
      if (access (buffer, F_OK) == 0)
        break;

      snprintf (buffer, sizeof (buffer), "%s/Muttrc-%s", PKGDATADIR, MUTT_VERSION);
      if (access (buffer, F_OK) == 0)
        break;

      snprintf (buffer, sizeof (buffer), "%s/Muttrc", PKGDATADIR);
    } while (0);
    if (access (buffer, F_OK) == 0)
    {
      if (source_rc (buffer, &err) != 0)
      {
	fputs (err.data, stderr);
	fputc ('\n', stderr);
	need_pause = 1;
      }
    }
  }

  /* Read the user's initialization file.  */
  for (LIST *config = Muttrc; config != NULL; config = config->next)
  {
    if (config->data)
    {
      if (!option(OPTNOCURSES))
        endwin();
      if (source_rc(config->data, &err) != 0)
      {
        fputs(err.data, stderr);
        fputc('\n', stderr);
        need_pause = 1;
      }
    }
  }

  if (execute_commands (commands) != 0)
    need_pause = 1;

  if (need_pause && !option (OPTNOCURSES))
  {
    if (mutt_any_key_to_continue (NULL) == -1)
      mutt_exit(1);
  }

  mutt_mkdir(Tempdir, S_IRWXU);

  mutt_read_histfile ();

#ifdef USE_NOTMUCH
  if (option (OPTVIRTSPOOLFILE) && VirtIncoming)
    mutt_str_replace(&Spoolfile, VirtIncoming->path);
#endif

  FREE (&err.data);
}

int mutt_get_hook_type (const char *name)
{
  const struct command_t *c = NULL;

  for (c = Commands ; c->name ; c++)
    if (c->func == mutt_parse_hook && (ascii_strcasecmp (c->name, name) == 0))
      return c->data;
  return 0;
}

static int parse_group_context (group_context_t **ctx, BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  while (mutt_strcasecmp (buf->data, "-group") == 0)
  {
    if (!MoreArgs (s))
    {
      strfcpy (err->data, _("-group: no group name"), err->dsize);
      goto bail;
    }

    mutt_extract_token (buf, s, 0);

    mutt_group_context_add (ctx, mutt_pattern_group (buf->data));

    if (!MoreArgs (s))
    {
      strfcpy (err->data, _("out of arguments"), err->dsize);
      goto bail;
    }

    mutt_extract_token (buf, s, 0);
  }

  return 0;

  bail:
  mutt_group_context_destroy (ctx);
  return -1;
}

#ifdef USE_NOTMUCH
static int parse_tag_transforms (BUFFER *b, BUFFER *s, unsigned long data, BUFFER *err)
{
  char *tmp = NULL;

  while (MoreArgs (s))
  {
    char *tag = NULL, *transform = NULL;

    mutt_extract_token (b, s, 0);
    if (b->data && *b->data)
      tag = safe_strdup (b->data);
    else
      continue;

    mutt_extract_token (b, s, 0);
    transform = safe_strdup (b->data);

    /* avoid duplicates */
    tmp = hash_find(TagTransforms, tag);
    if (tmp) {
      mutt_debug (3, "tag transform '%s' already registered as '%s'\n", tag, tmp);
      FREE(&tag);
      FREE(&transform);
      continue;
    }

    hash_insert(TagTransforms, tag, transform);
  }
  return 0;
}

static int parse_tag_formats (BUFFER *b, BUFFER *s, unsigned long data, BUFFER *err)
{
  char *tmp = NULL;

  while (MoreArgs (s))
  {
    char *tag = NULL, *format = NULL;

    mutt_extract_token (b, s, 0);
    if (b->data && *b->data)
      tag = safe_strdup (b->data);
    else
      continue;

    mutt_extract_token (b, s, 0);
    format = safe_strdup (b->data);

    /* avoid duplicates */
    tmp = hash_find(TagFormats, format);
    if (tmp) {
      mutt_debug (3, "tag format '%s' already registered as '%s'\n", format, tmp);
      FREE(&tag);
      FREE(&format);
      continue;
    }

    hash_insert(TagFormats, format, tag);
  }
  return 0;
}
#endif

const char* myvar_get (const char* var)
{
  myvar_t* cur = NULL;

  for (cur = MyVars; cur; cur = cur->next)
    if (mutt_strcmp (cur->name, var) == 0)
      return NONULL(cur->value);

  return NULL;
}

int mutt_label_complete (char *buffer, size_t len, int numtabs)
{
  char *pt = buffer;
  int spaces; /* keep track of the number of leading spaces on the line */

  if (!Context || !Context->label_hash)
    return 0;

  SKIPWS (buffer);
  spaces = buffer - pt;

  /* first TAB. Collect all the matches */
  if (numtabs == 1)
  {
    struct hash_elem *entry = NULL;
    struct hash_walk_state state;

    Num_matched = 0;
    strfcpy (User_typed, buffer, sizeof (User_typed));
    memset (Matches, 0, Matches_listsize);
    memset (Completed, 0, sizeof (Completed));
    memset (&state, 0, sizeof(state));
    while ((entry = hash_walk(Context->label_hash, &state)))
      candidate (Completed, User_typed, entry->key.strkey, sizeof (Completed));
    matches_ensure_morespace (Num_matched);
    qsort(Matches, Num_matched, sizeof(char *), (sort_t *) mutt_strcasecmp);
    Matches[Num_matched++] = User_typed;

    /* All matches are stored. Longest non-ambiguous string is ""
     * i.e. don't change 'buffer'. Fake successful return this time */
    if (User_typed[0] == 0)
      return 1;
  }

  if (Completed[0] == 0 && User_typed[0])
    return 0;

   /* Num_matched will _always_ be at least 1 since the initial
    * user-typed string is always stored */
  if (numtabs == 1 && Num_matched == 2)
    snprintf(Completed, sizeof(Completed), "%s", Matches[0]);
  else if (numtabs > 1 && Num_matched > 2)
    /* cycle thru all the matches */
    snprintf(Completed, sizeof(Completed), "%s",
             Matches[(numtabs - 2) % Num_matched]);

  /* return the completed label */
  strncpy (buffer, Completed, len - spaces);

  return 1;
}

