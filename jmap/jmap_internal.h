/**
 * Copyright (C) 2017 Robert Norris <robn@fastmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _MUTT_JMAP_INTERNAL_H
#define _MUTT_JMAP_INTERNAL_H 1

#include "config.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include "mutt.h"
#include "browser.h"
#include "buffy.h"
#include "globals.h"
#include "mx.h"
#include "account.h"
#include "bcache.h"
#include "jmap.h"
#include "mutt_curses.h"
#include "mutt_socket.h"
#include "mime.h"
#include <curl/curl.h>
#include <jansson.h>
#include <assert.h>

typedef struct jmap_context {

  /* calculated from the path. if a different path url is requested, the
   * checksum changes, and the context is reinitialised. I'm not even sure if
   * that can happen, but lets be safe. see jmap_context_prepare() for details */
  unsigned char checksum;

  /* allocation from full path but modified/exploded by url. unusable as-is */
  char *_path;

  /* everything you want is in here anyway */
  ciss_url_t url;

  /* active curl context for this server */
  CURL *curl;

  /* curl headers. must exist for the lifetime of the context */
  struct curl_slist *curl_headers;

  /* buffer for response body. "zero" it, fill it, make call */
  BUFFER *curl_body;

  /* mailbox id -> jmap_mailbox_t */
  HASH *mailbox_by_id;

  /* mailbox hierarchical name -> jmap_mailbox_t */
  HASH *mailbox_by_name;

} jmap_context_t;

/* context.c */
jmap_context_t *jmap_context_prepare(jmap_context_t *jctx, const char *path);
void jmap_context_free(jmap_context_t **jctxp);

/* mailbox.c */
extern int jmap_mailbox_open(CONTEXT *ctx);
extern int jmap_mailbox_open_append(CONTEXT *ctx, int flags);
extern int jmap_mailbox_close(CONTEXT *ctx);
extern int jmap_mailbox_check(CONTEXT *ctx, int *index_hint);
extern int jmap_mailbox_sync(CONTEXT *ctx, int *index_hint);

/* message.c */
extern int jmap_message_open(CONTEXT *ctx, MESSAGE *msg, int msgno);
extern int jmap_message_close(CONTEXT *ctx, MESSAGE *msg);
extern int jmap_message_commit(CONTEXT *ctx, MESSAGE *msg);
extern int jmap_message_open_new(MESSAGE *msg, CONTEXT *dest, HEADER *hdr);

/* client.c */
extern int jmap_client_call(jmap_context_t *jctx, const json_t *batch, json_t **rbatch);

#endif /* _MUTT_JMAP_INTERNAL_H */
