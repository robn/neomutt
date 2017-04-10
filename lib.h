/**
 * Copyright (C) 1996-2000,2007,2010,2012 Michael R. Elkins <me@mutt.org>
 * Copyright (C) 1999-2005,2007 Thomas Roessler <roessler@does-not-exist.org>
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

/* mutt functions which are generally useful. */

#ifndef _MUTT_LIB_H
#define _MUTT_LIB_H 1

# include <stdio.h>
# include <string.h>
# ifdef HAVE_UNISTD_H
#  include <unistd.h> /* needed for SEEK_SET */
# endif
# include <sys/types.h>
# include <sys/stat.h>
# include <time.h>
# include <limits.h>
# include <stdarg.h>
# include <stdbool.h>
# include <signal.h>
# include <ctype.h>

# ifndef _POSIX_PATH_MAX
#  include <limits.h>
# endif

# ifdef ENABLE_NLS
#  include <libintl.h>
# define _(a) gettext (a)
#  ifdef gettext_noop
#   define N_(a) gettext_noop (a)
#  else
#   define N_(a) (a)
#  endif
# else
#  define _(a) (a)
#  define N_(a) a
# endif

# define HUGE_STRING     8192
# define LONG_STRING     1024
# define STRING          256
# define SHORT_STRING    128

# define FREE(x) safe_free(x)
# define NONULL(x) x?x:""
# define ISSPACE(c) isspace((unsigned char)c)

# undef MAX
# undef MIN
# define MAX(a,b) ((a) < (b) ? (b) : (a))
# define MIN(a,b) ((a) < (b) ? (a) : (b))

/* Use this with care.  If the compiler can't see the array
 * definition, it obviously won't produce a correct result. */
#define mutt_array_size(x)  (sizeof (x) / sizeof ((x)[0]))

/* For mutt_format_string() justifications */
/* Making left 0 and center -1 is of course completely nonsensical, but
 * it retains compatibility for any patches that call mutt_format_string.
 * Once patches are updated to use FMT_*, these can be made sane. */
#define FMT_LEFT	0
#define FMT_RIGHT	1
#define FMT_CENTER	-1

/* this macro must check for *c == 0 since isspace(0) has unreliable behavior
   on some systems */
# define SKIPWS(c) while (*(c) && isspace ((unsigned char) *(c))) c++;

#define EMAIL_WSP " \t\r\n"

/* skip over WSP as defined by RFC5322.  This is used primarily for parsing
 * header fields. */
static inline char *skip_email_wsp(const char *s)
{
  if (s)
    return (char *)(s + strspn(s, EMAIL_WSP));
  return (char *)s;
}

static inline int is_email_wsp(char c)
{
  return c && (strchr(EMAIL_WSP, c) != NULL);
}


/*
 * These functions aren't defined in lib.c, but
 * they are used there.
 *
 * A non-mutt "implementation" (ahem) can be found in extlib.c.
 */


# ifndef _EXTLIB_C
extern void (*mutt_error) (const char *, ...);
# endif

void mutt_exit (int);


#ifdef DEBUG
extern char debugfilename[_POSIX_PATH_MAX];
extern FILE *debugfile;
extern int debuglevel;
extern char *debugfile_cmdline;
extern int debuglevel_cmdline;
void mutt_debug (int level, const char *fmt, ...);
#else
#define mutt_debug(...) do { } while (0)
#endif


/* Exit values used in send_msg() */
#define S_ERR 127
#define S_BKG 126

/* Flags for mutt_read_line() */
#define MUTT_CONT		(1<<0)		/* \-continuation */
#define MUTT_EOL		(1<<1)		/* don't strip \n/\r\n */

/* The actual library functions. */

FILE *safe_fopen(const char *path, const char *mode);

char *mutt_concatn_path(char *dst, size_t dstlen, const char *dir,
                        size_t dirlen, const char *fname, size_t fnamelen);
char *mutt_concat_path(char *d, const char *dir, const char *fname, size_t l);
char *mutt_read_line(char *s, size_t *size, FILE *fp, int *line, int flags);
char *mutt_skip_whitespace(char *p);
char *mutt_strlower(char *s);
const char *mutt_strchrnul(const char *s, char c);
char *mutt_substrcpy(char *dest, const char *beg, const char *end, size_t destlen);
char *mutt_substrdup(const char *begin, const char *end);
char *safe_strcat(char *d, size_t l, const char *s);
char *safe_strncat(char *d, size_t l, const char *s, size_t sl);
char *safe_strdup(const char *s);
char *strfcpy(char *dest, const char *src, size_t dlen);

/* strtol() wrappers with range checking; they return
 * 	 0 success
 * 	-1 format error
 * 	-2 overflow (for int and short)
 * the int pointer may be NULL to test only without conversion
 */
int mutt_atos (const char *, short *);
int mutt_atoi (const char *, int *);

const char *mutt_stristr (const char *, const char *);
const char *mutt_basename (const char *);

int mutt_copy_stream (FILE *, FILE *);
int mutt_copy_bytes (FILE *, FILE *, size_t);
int mutt_rx_sanitize_string (char *, size_t, const char *);
int mutt_strcasecmp (const char *, const char *);
int mutt_strcmp (const char *, const char *);
int mutt_strncasecmp (const char *, const char *, size_t);
int mutt_strncmp (const char *, const char *, size_t);
int mutt_strcoll (const char *, const char *);
int safe_asprintf (char **, const char *, ...);
int safe_open (const char *, int);
int safe_rename (const char *, const char *);
int safe_symlink (const char *, const char *);
int safe_fclose (FILE **);
int safe_fsync_close (FILE **);
int mutt_rmtree (const char *);
int mutt_mkdir(const char *path, mode_t mode);

size_t mutt_quote_filename (char *, size_t, const char *);
size_t mutt_strlen (const char *);

void *safe_calloc (size_t, size_t);
void *safe_malloc (size_t);
void mutt_nocurses_error (const char *, ...);
void mutt_remove_trailing_ws (char *);
void mutt_sanitize_filename (char *, short);
void mutt_str_replace (char **p, const char *s);
void mutt_str_adjust (char **p);
void mutt_unlink (const char *);
void safe_free (void *);
void safe_realloc (void *, size_t);
int  mutt_inbox_cmp (const char *a, const char *b);

const char *mutt_strsysexit(int e);

#endif /* _MUTT_LIB_H */
