/**
 * Copyright (C) 2004 Thomas Glanzmann <sithglan@stud.uni-erlangen.de>
 * Copyright (C) 2004 Tobias Werth <sitowert@stud.uni-erlangen.de>
 * Copyright (C) 2004 Brian Fundakowski Feldman <green@FreeBSD.org>
 * Copyright (C) 2016 Pietro Cerutti <gahr@gahr.ch>
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

#if !(defined(HAVE_BDB) || defined(HAVE_GDBM) || defined(HAVE_KC) || defined(HAVE_LMDB) || defined(HAVE_QDBM) || defined(HAVE_TC))
#error "No hcache backend defined"
#endif

#include <errno.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include "hcache.h"
#include "hcache-backend.h"
#include "hcversion.h"
#include "md5.h"

static unsigned int hcachever = 0x0;

/**
 * header_cache_t - header cache structure.
 *
 * This struct holds both the backend-agnostic and the backend-specific parts
 * of the header cache. Backend code MUST initialize the fetch, store,
 * delete and close function pointers in hcache_open, and MAY store
 * backend-specific context in the ctx pointer.
 */
struct header_cache
{
  char *folder;
  unsigned int crc;
  void *ctx;
};

typedef union
{
  struct timeval timeval;
  unsigned int uidvalidity;
} validate;

#define HCACHE_BACKEND(name) extern const hcache_ops_t hcache_##name##_ops;
HCACHE_BACKEND_LIST
#undef HCACHE_BACKEND

/* Keep this list sorted as it is in configure.ac to avoid user surprise if no
 * header_cache_backend is specified. */
const hcache_ops_t *hcache_ops[] = {
#ifdef HAVE_TC
  &hcache_tokyocabinet_ops,
#endif
#ifdef HAVE_KC
  &hcache_kyotocabinet_ops,
#endif
#ifdef HAVE_QDBM
  &hcache_qdbm_ops,
#endif
#ifdef HAVE_GDBM
  &hcache_gdbm_ops,
#endif
#ifdef HAVE_BDB
  &hcache_bdb_ops,
#endif
#ifdef HAVE_LMDB
  &hcache_lmdb_ops,
#endif
  NULL
};

static const hcache_ops_t *
hcache_get_backend_ops(const char *backend)
{
  const hcache_ops_t **ops = hcache_ops;

  if (!backend || !*backend)
  {
    return *ops;
  }

  for (; *ops; ++ops)
    if (strcmp(backend, (*ops)->name) == 0)
      break;

  return *ops;
}

#define hcache_get_ops() hcache_get_backend_ops(HeaderCacheBackend)

static void *
lazy_malloc(size_t siz)
{
  if (siz < 4096)
    siz = 4096;

  return safe_malloc(siz);
}

static void
lazy_realloc(void *ptr, size_t siz)
{
  void **p = (void **) ptr;

  if (p != NULL && siz < 4096)
    return;

  safe_realloc(ptr, siz);
}

static unsigned char *
dump_int(unsigned int i, unsigned char *d, int *off)
{
  lazy_realloc(&d, *off + sizeof (int));
  memcpy(d + *off, &i, sizeof (int));
  (*off) += sizeof (int);

  return d;
}

static void
restore_int(unsigned int *i, const unsigned char *d, int *off)
{
  memcpy(i, d + *off, sizeof (int));
  (*off) += sizeof (int);
}

static inline bool is_ascii (const char *p, size_t len) {
  register const char *s = p;
  while (s && (unsigned) (s - p) < len) {
    if ((*s & 0x80) != 0)
      return false;
    s++;
  }
  return true;
}

static unsigned char *
dump_char_size(char *c, unsigned char *d, int *off, ssize_t size, int convert)
{
  char *p = c;

  if (c == NULL)
  {
    size = 0;
    d = dump_int(size, d, off);
    return d;
  }

  if (convert && !is_ascii (c, size)) {
    p = mutt_substrdup (c, c + size);
    if (mutt_convert_string (&p, Charset, "utf-8", 0) == 0) {
      c = p;
      size = mutt_strlen (c) + 1;
    }
  }

  d = dump_int(size, d, off);
  lazy_realloc(&d, *off + size);
  memcpy(d + *off, p, size);
  *off += size;

  if (p != c)
    FREE(&p);

  return d;
}

static unsigned char *
dump_char(char *c, unsigned char *d, int *off, int convert)
{
  return dump_char_size (c, d, off, mutt_strlen (c) + 1, convert);
}

static void
restore_char(char **c, const unsigned char *d, int *off, int convert)
{
  unsigned int size;
  restore_int(&size, d, off);

  if (size == 0)
  {
    *c = NULL;
    return;
  }

  *c = safe_malloc(size);
  memcpy(*c, d + *off, size);
  if (convert && !is_ascii (*c, size)) {
    char *tmp = safe_strdup (*c);
    if (mutt_convert_string (&tmp, "utf-8", Charset, 0) == 0) {
      mutt_str_replace (c, tmp);
    } else {
      FREE(&tmp);
    }
  }
  *off += size;
}

static unsigned char *
dump_address(ADDRESS * a, unsigned char *d, int *off, int convert)
{
  unsigned int counter = 0;
  unsigned int start_off = *off;

  d = dump_int(0xdeadbeef, d, off);

  while (a)
  {
#ifdef EXACT_ADDRESS
    d = dump_char(a->val, d, off, convert);
#endif
    d = dump_char(a->personal, d, off, convert);
    d = dump_char(a->mailbox, d, off, 0);
    d = dump_int(a->group, d, off);
    a = a->next;
    counter++;
  }

  memcpy(d + start_off, &counter, sizeof (int));

  return d;
}

static void
restore_address(ADDRESS ** a, const unsigned char *d, int *off, int convert)
{
  unsigned int counter;

  restore_int(&counter, d, off);

  while (counter)
  {
    *a = rfc822_new_address();
#ifdef EXACT_ADDRESS
    restore_char(&(*a)->val, d, off, convert);
#endif
    restore_char(&(*a)->personal, d, off, convert);
    restore_char(&(*a)->mailbox, d, off, 0);
    restore_int((unsigned int *) &(*a)->group, d, off);
    a = &(*a)->next;
    counter--;
  }

  *a = NULL;
}

static unsigned char *
dump_list(LIST * l, unsigned char *d, int *off, int convert)
{
  unsigned int counter = 0;
  unsigned int start_off = *off;

  d = dump_int(0xdeadbeef, d, off);

  while (l)
  {
    d = dump_char(l->data, d, off, convert);
    l = l->next;
    counter++;
  }

  memcpy(d + start_off, &counter, sizeof (int));

  return d;
}

static void
restore_list(LIST ** l, const unsigned char *d, int *off, int convert)
{
  unsigned int counter;

  restore_int(&counter, d, off);

  while (counter)
  {
    *l = safe_malloc(sizeof (LIST));
    restore_char(&(*l)->data, d, off, convert);
    l = &(*l)->next;
    counter--;
  }

  *l = NULL;
}

static unsigned char *
dump_buffer(BUFFER * b, unsigned char *d, int *off, int convert)
{
  if (!b)
  {
    d = dump_int(0, d, off);
    return d;
  }
  else
    d = dump_int(1, d, off);

  d = dump_char_size(b->data, d, off, b->dsize + 1, convert);
  d = dump_int(b->dptr - b->data, d, off);
  d = dump_int(b->dsize, d, off);
  d = dump_int(b->destroy, d, off);

  return d;
}

static void
restore_buffer(BUFFER ** b, const unsigned char *d, int *off, int convert)
{
  unsigned int used;
  unsigned int offset;
  restore_int(&used, d, off);
  if (!used)
  {
    return;
  }

  *b = safe_malloc(sizeof (BUFFER));

  restore_char(&(*b)->data, d, off, convert);
  restore_int(&offset, d, off);
  (*b)->dptr = (*b)->data + offset;
  restore_int (&used, d, off);
  (*b)->dsize = used;
  restore_int (&used, d, off);
  (*b)->destroy = used;
}

static unsigned char *
dump_parameter(PARAMETER * p, unsigned char *d, int *off, int convert)
{
  unsigned int counter = 0;
  unsigned int start_off = *off;

  d = dump_int(0xdeadbeef, d, off);

  while (p)
  {
    d = dump_char(p->attribute, d, off, 0);
    d = dump_char(p->value, d, off, convert);
    p = p->next;
    counter++;
  }

  memcpy(d + start_off, &counter, sizeof (int));

  return d;
}

static void
restore_parameter(PARAMETER ** p, const unsigned char *d, int *off, int convert)
{
  unsigned int counter;

  restore_int(&counter, d, off);

  while (counter)
  {
    *p = safe_malloc(sizeof (PARAMETER));
    restore_char(&(*p)->attribute, d, off, 0);
    restore_char(&(*p)->value, d, off, convert);
    p = &(*p)->next;
    counter--;
  }

  *p = NULL;
}

static unsigned char *
dump_body(BODY * c, unsigned char *d, int *off, int convert)
{
  BODY nb;

  memcpy (&nb, c, sizeof (BODY));

  /* some fields are not safe to cache */
  nb.content = NULL;
  nb.charset = NULL;
  nb.next = NULL;
  nb.parts = NULL;
  nb.hdr = NULL;
  nb.aptr = NULL;

  lazy_realloc(&d, *off + sizeof (BODY));
  memcpy(d + *off, &nb, sizeof (BODY));
  *off += sizeof (BODY);

  d = dump_char(nb.xtype, d, off, 0);
  d = dump_char(nb.subtype, d, off, 0);

  d = dump_parameter(nb.parameter, d, off, convert);

  d = dump_char(nb.description, d, off, convert);
  d = dump_char(nb.form_name, d, off, convert);
  d = dump_char(nb.filename, d, off, convert);
  d = dump_char(nb.d_filename, d, off, convert);

  return d;
}

static void
restore_body(BODY * c, const unsigned char *d, int *off, int convert)
{
  memcpy(c, d + *off, sizeof (BODY));
  *off += sizeof (BODY);

  restore_char(&c->xtype, d, off, 0);
  restore_char(&c->subtype, d, off, 0);

  restore_parameter(&c->parameter, d, off, convert);

  restore_char(&c->description, d, off, convert);
  restore_char(&c->form_name, d, off, convert);
  restore_char(&c->filename, d, off, convert);
  restore_char(&c->d_filename, d, off, convert);
}

static unsigned char *
dump_envelope(ENVELOPE * e, unsigned char *d, int *off, int convert)
{
  d = dump_address(e->return_path, d, off, convert);
  d = dump_address(e->from, d, off, convert);
  d = dump_address(e->to, d, off, convert);
  d = dump_address(e->cc, d, off, convert);
  d = dump_address(e->bcc, d, off, convert);
  d = dump_address(e->sender, d, off, convert);
  d = dump_address(e->reply_to, d, off, convert);
  d = dump_address(e->mail_followup_to, d, off, convert);

  d = dump_char(e->list_post, d, off, convert);
  d = dump_char(e->subject, d, off, convert);

  if (e->real_subj)
    d = dump_int(e->real_subj - e->subject, d, off);
  else
    d = dump_int(-1, d, off);

  d = dump_char(e->message_id, d, off, 0);
  d = dump_char(e->supersedes, d, off, 0);
  d = dump_char(e->date, d, off, 0);
  d = dump_char(e->x_label, d, off, convert);

  d = dump_buffer(e->spam, d, off, convert);

  d = dump_list(e->references, d, off, 0);
  d = dump_list(e->in_reply_to, d, off, 0);
  d = dump_list(e->userhdrs, d, off, convert);

#ifdef USE_NNTP
  d = dump_char(e->xref, d, off, 0);
  d = dump_char(e->followup_to, d, off, 0);
  d = dump_char(e->x_comment_to, d, off, convert);
#endif

  return d;
}

static void
restore_envelope(ENVELOPE * e, const unsigned char *d, int *off, int convert)
{
  int real_subj_off;

  restore_address(&e->return_path, d, off, convert);
  restore_address(&e->from, d, off, convert);
  restore_address(&e->to, d, off, convert);
  restore_address(&e->cc, d, off, convert);
  restore_address(&e->bcc, d, off, convert);
  restore_address(&e->sender, d, off, convert);
  restore_address(&e->reply_to, d, off, convert);
  restore_address(&e->mail_followup_to, d, off, convert);

  restore_char(&e->list_post, d, off, convert);
  restore_char(&e->subject, d, off, convert);
  restore_int((unsigned int *) (&real_subj_off), d, off);

  if (0 <= real_subj_off)
    e->real_subj = e->subject + real_subj_off;
  else
    e->real_subj = NULL;

  restore_char(&e->message_id, d, off, 0);
  restore_char(&e->supersedes, d, off, 0);
  restore_char(&e->date, d, off, 0);
  restore_char(&e->x_label, d, off, convert);

  restore_buffer(&e->spam, d, off, convert);

  restore_list(&e->references, d, off, 0);
  restore_list(&e->in_reply_to, d, off, 0);
  restore_list(&e->userhdrs, d, off, convert);

#ifdef USE_NNTP
  restore_char(&e->xref, d, off, 0);
  restore_char(&e->followup_to, d, off, 0);
  restore_char(&e->x_comment_to, d, off, convert);
#endif
}

static int
crc_matches(const char *d, unsigned int crc)
{
  int off = sizeof (validate);
  unsigned int mycrc = 0;

  if (!d)
    return 0;

  restore_int(&mycrc, (unsigned char *) d, &off);

  return (crc == mycrc);
}

/**
 * create_hcache_dir - Create parent dirs for the hcache database
 * @path: Database filename
 *
 * @retval 1 Success
 * @retval 0 Failure (errno set)
 */
static int create_hcache_dir(const char *path)
{
  if (!path)
    return 0;

  static char dir[_POSIX_PATH_MAX];
  strfcpy (dir, path, sizeof(dir));

  char *p = strrchr (dir, '/');
  if (!p)
    return 1;

  *p = 0;
  if (mutt_mkdir (dir, S_IRWXU | S_IRWXG | S_IRWXO) == 0)
    return 1;

  mutt_error (_("Can't create %s: %s."), dir, strerror (errno));
  mutt_sleep (2);
  return 0;
}

/**
 * hcache_per_folder - Generate the hcache pathname
 * @param path   Base directory, from $header_cache
 * @param folder Mailbox name (including protocol)
 * @param namer  Callback to generate database filename
 *
 * @return Full pathname to the database (to be generated)
 *         (path must be freed by the caller)
 *
 * Generate the pathname for the hcache database, it will be of the form:
 *     BASE/FOLDER/NAME-SUFFIX
 *
 * BASE :  Base directory (@path)
 * FOLDER: Mailbox name (@folder)
 * NAME:   Create by @namer, or md5sum of @folder
 * SUFFIX: Character set (if ICONV isn't being used)
 *
 * This function will create any parent directories needed, so the caller just
 * needs to create the database file.
 *
 * If @path exists and is a directory, it is used.
 * If @path has a trailing '/' it is assumed to be a directory.
 * If ICONV isn't being used, then a suffix is added to the path, e.g. '-utf-8'.
 * Otherise @path is assumed to be a file.
 */
static const char *hcache_per_folder(const char *path, const char *folder,
                                          hcache_namer_t namer)
{
  static char hcpath[_POSIX_PATH_MAX];
  char suffix[32] = "";
  struct stat sb;

  int plen = mutt_strlen(path);
  int ret = stat(path, &sb);
  int slash = (path[plen - 1] == '/');

  if (((ret == 0) && !S_ISDIR(sb.st_mode)) || ((ret == -1) && !slash))
  {
      /* An existing file or a non-existing path not ending with a slash */
      snprintf(hcpath, sizeof(hcpath), "%s%s", path, suffix);
      return hcpath;
  }

  /* We have a directory - no matter whether it exists, or not */

  if (namer)
  {
    /* We have a mailbox-specific namer function */
    snprintf(hcpath, sizeof(hcpath), "%s%s", path, slash ? "" : "/");
    if (!slash)
      plen++;

    ret = namer(folder, hcpath + plen, sizeof(hcpath) - plen);
  }
  else
  {
    unsigned char m[16]; /* binary md5sum */
    char name[33];
    md5_buffer(folder, strlen(folder), &m);
    snprintf(name, sizeof(name),
             "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
             m[0], m[1], m[2],  m[3],  m[4],  m[5],  m[6],  m[7],
             m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);

    ret = snprintf(hcpath, sizeof(hcpath), "%s%s%s%s", path, slash ? "" : "/",
        name, suffix);
  }

  if (ret < 0) /* namer or fprintf failed.. should not happen */
    return path;

  create_hcache_dir(hcpath);
  return hcpath;
}

/* This function transforms a header into a char so that it is useable by
 * db_store.
 */
static void *
hcache_dump(header_cache_t *h, HEADER * header, int *off,
                 unsigned int uidvalidity)
{
  unsigned char *d = NULL;
  HEADER nh;
  int convert = !Charset_is_utf8;

  *off = 0;
  d = lazy_malloc(sizeof (validate));

  if (uidvalidity == 0)
  {
    struct timeval now;
    gettimeofday(&now, NULL);
    memcpy(d, &now, sizeof (struct timeval));
  }
  else
    memcpy(d, &uidvalidity, sizeof (uidvalidity));
  *off += sizeof (validate);

  d = dump_int(h->crc, d, off);

  lazy_realloc(&d, *off + sizeof (HEADER));
  memcpy(&nh, header, sizeof (HEADER));

  /* some fields are not safe to cache */
  nh.tagged = false;
  nh.changed = false;
  nh.threaded = false;
  nh.recip_valid = false;
  nh.searched = false;
  nh.matched = false;
  nh.collapsed = false;
  nh.limited = false;
  nh.num_hidden = 0;
  nh.recipient = 0;
  nh.pair = 0;
  nh.attach_valid = false;
  nh.path = NULL;
  nh.tree = NULL;
  nh.thread = NULL;
  nh.tags = NULL;
#ifdef MIXMASTER
  nh.chain = NULL;
#endif
#if defined(USE_POP) || defined(USE_IMAP)
  nh.data = NULL;
#endif

  memcpy(d + *off, &nh, sizeof (HEADER));
  *off += sizeof (HEADER);

  d = dump_envelope(nh.env, d, off, convert);
  d = dump_body(nh.content, d, off, convert);
  d = dump_char(nh.maildir_flags, d, off, convert);

  return d;
}

HEADER *
mutt_hcache_restore(const unsigned char *d)
{
  int off = 0;
  HEADER *h = mutt_new_header();
  int convert = !Charset_is_utf8;

  /* skip validate */
  off += sizeof (validate);

  /* skip crc */
  off += sizeof (unsigned int);

  memcpy(h, d + off, sizeof (HEADER));
  off += sizeof (HEADER);

  h->env = mutt_new_envelope();
  restore_envelope(h->env, d, &off, convert);

  h->content = mutt_new_body();
  restore_body(h->content, d, &off, convert);

  restore_char(&h->maildir_flags, d, &off, convert);

  return h;
}

static char* get_foldername(const char *folder)
{
  char *p = NULL;
  char path[_POSIX_PATH_MAX];

  mutt_encode_path (path, sizeof (path), folder);

  /* if the folder is local, canonify the path to avoid
   * to ensure equivalent paths share the hcache */
  p = safe_malloc (PATH_MAX+1);
  if (!realpath (path, p))
    mutt_str_replace (&p, path);

  return p;
}

header_cache_t *
mutt_hcache_open(const char *path, const char *folder, hcache_namer_t namer)
{
  const hcache_ops_t *ops = hcache_get_ops();
  if (!ops)
    return NULL;

  header_cache_t *h = safe_calloc(1, sizeof (header_cache_t));

  /* Calculate the current hcache version from dynamic configuration */
  if (hcachever == 0x0) {
    union {
      unsigned char charval[16];
      unsigned int intval;
    } digest;
    struct md5_ctx ctx;
    REPLACE_LIST *spam = NULL;
    RX_LIST *nospam = NULL;

    hcachever = HCACHEVER;

    md5_init_ctx(&ctx);

    /* Seed with the compiled-in header structure hash */
    md5_process_bytes(&hcachever, sizeof(hcachever), &ctx);

    /* Mix in user's spam list */
    for (spam = SpamList; spam; spam = spam->next)
    {
      md5_process_bytes(spam->rx->pattern, strlen(spam->rx->pattern), &ctx);
      md5_process_bytes(spam->template, strlen(spam->template), &ctx);
    }

    /* Mix in user's nospam list */
    for (nospam = NoSpamList; nospam; nospam = nospam->next)
    {
      md5_process_bytes(nospam->rx->pattern, strlen(nospam->rx->pattern), &ctx);
    }

    /* Get a hash and take its bytes as an (unsigned int) hash version */
    md5_finish_ctx(&ctx, digest.charval);
    hcachever = digest.intval;
  }

  h->folder = get_foldername(folder);
  h->crc = hcachever;

  if (!path || path[0] == '\0')
  {
    FREE(&h->folder);
    FREE(&h);
    return NULL;
  }

  path = hcache_per_folder(path, h->folder, namer);

  h->ctx = ops->open(path);
  if (h->ctx)
    return h;
  else
  {
    /* remove a possibly incompatible version */
    if (unlink (path) == 0)
    {
      h->ctx = ops->open(path);
      if (h->ctx)
        return h;
    }
    FREE(&h->folder);
    FREE(&h);

    return NULL;
  }
}

void mutt_hcache_close(header_cache_t *h)
{
  const hcache_ops_t *ops = hcache_get_ops();
  if (!h || !ops)
    return;

  ops->close(&h->ctx);
  FREE (&h->folder);
  FREE (&h);
}

void *
mutt_hcache_fetch(header_cache_t *h, const char *key, size_t keylen)
{
  void* data = NULL;

  data = mutt_hcache_fetch_raw (h, key, keylen);
  if (!data)
  {
    return NULL;
  }

  if (!crc_matches(data, h->crc))
  {
    mutt_hcache_free(h, &data);
    return NULL;
  }

  return data;
}

void *
mutt_hcache_fetch_raw(header_cache_t *h, const char *key, size_t keylen)
{
  char path[_POSIX_PATH_MAX];
  const hcache_ops_t *ops = hcache_get_ops();

  if (!h || !ops)
    return NULL;

  keylen = snprintf(path, sizeof(path), "%s%s", h->folder, key);

  return ops->fetch(h->ctx, path, keylen);
}

void
mutt_hcache_free(header_cache_t *h, void **data)
{
  const hcache_ops_t *ops = hcache_get_ops();

  if (!h || !ops)
    return;

  ops->free(h->ctx, data); /* __MEM_CHECKED__ */
}

int
mutt_hcache_store(header_cache_t *h, const char *key, size_t keylen,
                  HEADER * header, unsigned int uidvalidity)
{
  char* data = NULL;
  int dlen;
  int ret;

  if (!h)
    return -1;

  data = hcache_dump(h, header, &dlen, uidvalidity);
  ret = mutt_hcache_store_raw (h, key, keylen, data, dlen);

  FREE(&data);

  return ret;
}

int
mutt_hcache_store_raw(header_cache_t *h, const char* key, size_t keylen,
                      void* data, size_t dlen)
{
  char path[_POSIX_PATH_MAX];
  const hcache_ops_t *ops = hcache_get_ops();

  if (!h || !ops)
    return -1;

  keylen = snprintf(path, sizeof(path), "%s%s", h->folder, key);

  return ops->store(h->ctx, path, keylen, data, dlen);
}

int
mutt_hcache_delete(header_cache_t *h, const char *key, size_t keylen)
{
  char path[_POSIX_PATH_MAX];
  const hcache_ops_t *ops = hcache_get_ops();

  if (!h)
    return -1;

  keylen = snprintf(path, sizeof(path), "%s%s", h->folder, key);

  return ops->delete(h->ctx, path, keylen);
}

const char *
mutt_hcache_backend_list(void)
{
  char tmp[STRING] = {0};
  const hcache_ops_t **ops = hcache_ops;
  size_t len = 0;

  for (; *ops; ++ops)
  {
    if (len != 0)
    {
      len += snprintf(tmp+len, STRING-len, ", ");
    }
    len += snprintf(tmp+len, STRING-len, "%s", (*ops)->name);
  }

  return safe_strdup(tmp);
}

int
mutt_hcache_is_valid_backend(const char *s)
{
  return hcache_get_backend_ops(s) != NULL;
}
