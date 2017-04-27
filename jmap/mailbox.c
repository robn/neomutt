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
  char id[256]; // XXX hardcoded
} jmap_mailbox_t;


void _jmap_mailbox_expand_name_recursive(const char *id, HASH *mailbox_by_id, BUFFER *namebuf) {
  json_t *mailbox = hash_find(mailbox_by_id, id);
  if (!mailbox) return;

  const char *parent_id = json_string_value(json_object_get(mailbox, "parentId"));
  if (parent_id) {
    _jmap_mailbox_expand_name_recursive(parent_id, mailbox_by_id, namebuf);
    mutt_buffer_addch(namebuf, '/');
  }

  const char *name = json_string_value(json_object_get(mailbox, "name"));
  mutt_buffer_addstr(namebuf, name);
}

int _jmap_mailbox_get(jmap_context_t *jctx, const char *path, jmap_mailbox_t *mailbox)
{
  const char *want_name = strchr(path, '?');
  if (!want_name || !*++want_name) return -1;

  json_t *batch = json_pack("[[s {} s]]", "getMailboxes", "a1");
  json_t *rbatch;
  int rc = jmap_client_call(jctx, batch, &rbatch);
  if (rc) return rc;

  // XXX err/ret through here
  json_error_t jerr;
  const char *rmethod, *rtag;
  json_t *rmailboxes;
  if (json_unpack_ex(rbatch, &jerr, 0, "[[s {s:o} s]]", &rmethod, "list", &rmailboxes, &rtag)) {
    mutt_debug(1, "jmap: couldn't unpack mailboxes response: %s\n", jerr.text);
    return -1;
  }

  HASH *mailbox_by_id = hash_create(64, 0);

  size_t i;
  json_t *rmailbox;
  json_array_foreach(rmailboxes, i, rmailbox) {
    const char *id = json_string_value(json_object_get(rmailbox, "id"));
#ifdef DEBUG
    if (debuglevel > 1) {
      const char *name = json_string_value(json_object_get(rmailbox, "name"));
      const char *role = json_string_value(json_object_get(rmailbox, "role"));
      mutt_debug(2, "jmap: mailbox %s name %s role %s\n", id, name, role ? role : "[null]");
    }
#endif
    hash_insert(mailbox_by_id, id, rmailbox);
  }

  HASH *id_by_name = hash_create(64, MUTT_HASH_STRDUP_KEYS);

  BUFFER *namebuf = mutt_buffer_new();
  json_array_foreach(rmailboxes, i, rmailbox) {
    const char *id = json_string_value(json_object_get(rmailbox, "id"));
    namebuf->dptr = namebuf->data;
    _jmap_mailbox_expand_name_recursive(id, mailbox_by_id, namebuf);
    hash_insert(id_by_name, namebuf->data, (void *) id);
    mutt_debug(2, "jmap: mapped mailbox name %s => %s\n", namebuf->data, id);
  }
  mutt_buffer_free(&namebuf);

  const char *id = hash_find(id_by_name, want_name);
  if (id) {
    strncpy(mailbox->id, id, sizeof(mailbox->id));
    // XXX free things
    return 0;
  }

  // XXX free things

  // not found
  return -1;
}

int jmap_mailbox_open(CONTEXT *ctx)
{
  ctx->data = jmap_context_prepare(ctx->data, ctx->path);
  jmap_context_t *jctx = (jmap_context_t *) ctx->data;

  jmap_mailbox_t mailbox;
  int rc = _jmap_mailbox_get(jctx, ctx->path, &mailbox);
  if (rc) return rc;

  mutt_debug(1, "jmap: got mailbox '%s'\n", mailbox.id);

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
