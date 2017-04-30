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

#ifdef DEBUG
static int _jmap_curl_debug(CURL *handle, curl_infotype type, char *data, size_t size, void *userptr)
{
  mutt_debug(3, "jmap: curl [%d]: %.*s", type, size, data);
  return 0;
}
#endif

static size_t _jmap_curl_write(char *buf, size_t size, size_t nmemb, void *data)
{
  mutt_buffer_add((BUFFER *) data, buf, size*nmemb);
  return size*nmemb;
}

jmap_context_t *jmap_context_prepare(CONTEXT *ctx)
{
  jmap_context_t *jctx = ctx->data;
  const char *path = ctx->realpath;

  /* calculate checksum; simple XOR over the path (excluding the trailing
   * ?folder selector, if any) */
  unsigned char checksum = 0;
  for (const char *c = path; *c && *c != '?'; c++)
    checksum ^= *c;

  /* if same checksum, we get to keep our stuff */
  if (jctx && checksum == jctx->checksum)
    return jctx;

  /* invalid, free the old one if it exists */
  if (jctx)
    jmap_context_free(&jctx);

  /* and make a new one! */
  jctx = ctx->data = safe_calloc(1, sizeof(jmap_context_t));
  jctx->checksum = checksum;

  /* explode the url */
  jctx->_path = safe_strdup(path);
  url_parse_ciss(&jctx->url, jctx->_path);
  if (!jctx->url.port) jctx->url.port = 443;

  /* clear any query part. the context is about whole server state */
  char *q = strchr(jctx->url.path, '?');
  if (q) *q = '\0';

  /* prepare curl context */
  curl_global_init(CURL_GLOBAL_ALL);
  jctx->curl = curl_easy_init();

  /* construct the URL for curl */
  BUFFER *baseurl = mutt_buffer_new();
  mutt_buffer_printf(baseurl, "https://%s:%d/%s", jctx->url.host, jctx->url.port, jctx->url.path);
  curl_easy_setopt(jctx->curl, CURLOPT_URL, baseurl->data);
  mutt_buffer_free(&baseurl);

  /* auth */
  if (jctx->url.user) {
    curl_easy_setopt(jctx->curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(jctx->curl, CURLOPT_USERNAME, jctx->url.user);
    curl_easy_setopt(jctx->curl, CURLOPT_PASSWORD, jctx->url.pass);
  }

#ifdef DEBUG
  /* thread curl debugging through mutt debug */
  if (debuglevel > 2) {
    curl_easy_setopt(jctx->curl, CURLOPT_VERBOSE, 1);
    curl_easy_setopt(jctx->curl, CURLOPT_DEBUGFUNCTION, _jmap_curl_debug);
  }
#endif

  /* everything is POST */
  curl_easy_setopt(jctx->curl, CURLOPT_POST, 1);

  /* standard headers for JMAP */
  jctx->curl_headers = curl_slist_append(jctx->curl_headers, "User-agent: mutt-jmap/0.1");
  jctx->curl_headers = curl_slist_append(jctx->curl_headers, "Content-type: application/json");
  jctx->curl_headers = curl_slist_append(jctx->curl_headers, "Accept: application/json");
  curl_easy_setopt(jctx->curl, CURLOPT_HTTPHEADER, jctx->curl_headers);

  /* prep the body buffer */
  jctx->curl_body = mutt_buffer_new();
  curl_easy_setopt(jctx->curl, CURLOPT_WRITEFUNCTION, _jmap_curl_write);
  curl_easy_setopt(jctx->curl, CURLOPT_WRITEDATA, jctx->curl_body);

  return jctx;
}

void jmap_context_free(jmap_context_t **jctxp) {
  jmap_context_t *jctx = *jctxp;
  FREE(&jctx->_path);
  curl_easy_cleanup(jctx->curl);
  curl_slist_free_all(jctx->curl_headers);
  mutt_buffer_free(&jctx->curl_body);
  FREE(jctxp);
}
