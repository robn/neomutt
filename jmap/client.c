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
#include "buffer.h"
#include <curl/curl.h>

static int _jmap_client_json_dump_callback(const char *buf, size_t size, void *data)
{
  BUFFER *out = (BUFFER *) data;
  mutt_buffer_add(out, buf, size);
  return 0;
}

int jmap_client_call(jmap_context_t *jctx, const json_t *batch, json_t **rbatch, const char *progressname)
{
  jctx->curl_body->dptr = jctx->curl_body->data;

  mutt_progress_init(&jctx->progress, progressname, MUTT_PROGRESS_MSG, NetInc, 0);

  json_dump_callback(batch, _jmap_client_json_dump_callback, jctx->curl_body, JSON_COMPACT); // XXX ret/err
  uintptr_t req_content_length = jctx->curl_body->dptr - jctx->curl_body->data;

  curl_easy_setopt(jctx->curl, CURLOPT_POSTFIELDS, jctx->curl_body->data);
  curl_easy_setopt(jctx->curl, CURLOPT_POSTFIELDSIZE, req_content_length);

  CURLcode rc = curl_easy_perform(jctx->curl);
  if (rc != CURLE_OK) {
    mutt_debug(1, "jmap: request failed: %s\n", curl_easy_strerror(rc));
    return -1;
  }

  uint32_t code;
  curl_easy_getinfo(jctx->curl, CURLINFO_RESPONSE_CODE, &code);
  if (code != 200) {
    mutt_debug(1, "jmap: HTTP request returned: %d\n", code);
    return -1;
  }

  json_error_t jerr;
  *rbatch = json_loads(jctx->curl_body->data + req_content_length, 0, &jerr);
  if (!*rbatch) {
    mutt_debug(1, "jmap: couldn't parse JMAP response: %s\n", jerr.text);
    return -1;
  }

  return 0;
}
