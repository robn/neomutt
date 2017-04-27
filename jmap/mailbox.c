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

#include "jmap_internal.h"

typedef struct jmap_mailbox {
  char *id;
  char *name;
  char *parent_id;
} jmap_mailbox_t;

void _jmap_mailbox_free(void *jmailboxv)
{
  jmap_mailbox_t *jmailbox = (jmap_mailbox_t *) jmailboxv;
  FREE(&jmailbox->id);
  FREE(&jmailbox->name);
  FREE(&jmailbox->parent_id);
  FREE(&jmailbox);
}

void _jmap_mailbox_expand_name_recursive(jmap_context_t *jctx, const char *id, BUFFER *namebuf) {
  jmap_mailbox_t *jmailbox = hash_find(jctx->mailbox_by_id, id);
  if (!jmailbox) return;

  if (jmailbox->parent_id) {
    _jmap_mailbox_expand_name_recursive(jctx, jmailbox->parent_id, namebuf);
    mutt_buffer_addch(namebuf, '/');
  }

  mutt_buffer_addstr(namebuf, jmailbox->name);
}

int _jmap_mailbox_refresh(jmap_context_t *jctx)
{
  /* XXX track state, if we have a last state do getMailboxUpdates instead */
  json_t *batch = json_pack("[[s {} s]]", "getMailboxes", "a1");
  json_t *rbatch = NULL;
  int rc = jmap_client_call(jctx, batch, &rbatch);
  json_decref(batch);
  if (rc) {
    if (rbatch) json_decref(rbatch);
    return rc;
  }

  json_error_t jerr;
  const char *rmethod, *rtag;
  json_t *rmailboxes;
  if (json_unpack_ex(rbatch, &jerr, 0, "[[s {s:o} s]]", &rmethod, "list", &rmailboxes, &rtag)) {
    mutt_debug(1, "jmap: couldn't unpack mailboxes response: %s\n", jerr.text);
    json_decref(rbatch);
    return -1;
  }

  if (jctx->mailbox_by_id)
    hash_destroy(&jctx->mailbox_by_id, _jmap_mailbox_free);
  if (jctx->mailbox_by_name)
    hash_destroy(&jctx->mailbox_by_name, NULL);

  jctx->mailbox_by_id = hash_create(64, 0);

  size_t i;
  json_t *rmailbox;
  json_array_foreach(rmailboxes, i, rmailbox) {
    const char *id = json_string_value(json_object_get(rmailbox, "id"));
    const char *name = json_string_value(json_object_get(rmailbox, "name"));
    const char *parent_id = json_string_value(json_object_get(rmailbox, "parentId"));

#ifdef DEBUG
    if (debuglevel > 1) {
      const char *role = json_string_value(json_object_get(rmailbox, "role"));
      mutt_debug(3, "jmap: mailbox %s name %s role %s\n", id, name, role ? role : "[null]");
    }
#endif

    jmap_mailbox_t *jmailbox = safe_calloc(1, sizeof(jmap_mailbox_t));

    jmailbox->id = safe_strdup(id);
    jmailbox->name = safe_strdup(name);
    if (parent_id) jmailbox->parent_id = safe_strdup(parent_id);

    hash_insert(jctx->mailbox_by_id, jmailbox->id, jmailbox);
  }

  jctx->mailbox_by_name = hash_create(64, MUTT_HASH_STRDUP_KEYS);

  BUFFER *namebuf = mutt_buffer_new();
  struct hash_walk_state state;
  memset(&state, 0, sizeof(struct hash_walk_state));
  struct hash_elem *elem = NULL;
  while ((elem = hash_walk(jctx->mailbox_by_id, &state))) {
    jmap_mailbox_t *jmailbox = elem->data;
    namebuf->dptr = namebuf->data;
    _jmap_mailbox_expand_name_recursive(jctx, jmailbox->id, namebuf);
    hash_insert(jctx->mailbox_by_name, namebuf->data, jmailbox);
    mutt_debug(3, "jmap: mapped mailbox name %s => %s\n", namebuf->data, jmailbox->id);
  }
  mutt_buffer_free(&namebuf);

  json_decref(rbatch);

  return 0;
}

int _jmap_mailbox_get(jmap_context_t *jctx, const char *path, jmap_mailbox_t **jmailboxp)
{
  const char *want_name = strchr(path, '?');
  if (!want_name || !*++want_name) return -1;

  int rc = _jmap_mailbox_refresh(jctx);
  if (rc) return rc;

  *jmailboxp = hash_find(jctx->mailbox_by_name, want_name);

  return 0;
}

int jmap_mailbox_open(CONTEXT *ctx)
{
  ctx->data = jmap_context_prepare(ctx->data, ctx->path);
  jmap_context_t *jctx = (jmap_context_t *) ctx->data;

  jmap_mailbox_t *jmailbox;
  int rc = _jmap_mailbox_get(jctx, ctx->path, &jmailbox);
  if (rc) return rc;

  mutt_debug(1, "jmap: got mailbox '%s'\n", jmailbox->id);

  return 0;
}

int jmap_mailbox_open_append(CONTEXT *ctx, int flags)
{
  return -1;
}

int jmap_mailbox_close(CONTEXT *ctx)
{
  return -1;
}

int jmap_mailbox_check(CONTEXT *ctx, int *index_hint)
{
  return -1;
}

int jmap_mailbox_sync(CONTEXT *ctx, int *index_hint)
{
  return -1;
}
