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

static int _jmap_client_set_url_auth(CONTEXT *ctx, CURL *handle)
{
  ciss_url_t url;
  char *c = safe_strdup(ctx->path);
  url_parse_ciss(&url, c);

  if (!url.port) url.port = 443;
  char *q = strchr(url.path, '?');
  if (q) *q = '\0';

  static BUFFER *baseurl = 0;
  if (!baseurl)
    baseurl = mutt_buffer_new();
  else
    baseurl->dptr = baseurl->data;

  mutt_buffer_printf(baseurl, "https://%s:%d/%s", url.host, url.port, url.path);

  curl_easy_setopt(handle, CURLOPT_URL, baseurl->data);

  if (url.user) {
    curl_easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(handle, CURLOPT_USERNAME, url.user);
    curl_easy_setopt(handle, CURLOPT_PASSWORD, url.pass);
  }

  FREE(&c);

  return 0;
}

static int _jmap_client_curl_debug(CURL *handle, curl_infotype type, char *data, size_t size, void *userptr)
{
  mutt_debug(3, "jmap: curl [%d]: %.*s", type, size, data);
}

static size_t _jmap_client_curl_write(char *buf, size_t size, size_t nmemb, void *data)
{
  BUFFER *in = (BUFFER *) data;
  mutt_buffer_add(in, buf, size*nmemb);
  return size*nmemb;
}

static int _jmap_client_json_dump_callback(const char *buf, size_t size, void *data)
{
  BUFFER *out = (BUFFER *) data;
  mutt_buffer_add(out, buf, size);
  return 0;
}

int jmap_client_call(CONTEXT *ctx, const json_t *batch, json_t **rbatch)
{
  static CURL *curl_jmap = 0; // XXX in the context somewhere
  static struct curl_slist *curl_jmap_headers = 0;
  static BUFFER *body_out = 0, *body_in = 0;

  if (!body_out) body_out = mutt_buffer_new();
  if (!body_in)  body_in  = mutt_buffer_new();

  if (!curl_jmap) {
    curl_jmap = curl_easy_init();

#ifdef DEBUG
    if (debuglevel > 2) {
      curl_easy_setopt(curl_jmap, CURLOPT_VERBOSE, 1);
      curl_easy_setopt(curl_jmap, CURLOPT_DEBUGFUNCTION, _jmap_client_curl_debug);
    }
#endif

    curl_easy_setopt(curl_jmap, CURLOPT_POST, 1);

    curl_jmap_headers = curl_slist_append(curl_jmap_headers, "User-agent: mutt-jmap/0.1");
    curl_jmap_headers = curl_slist_append(curl_jmap_headers, "Content-type: application/json");
    curl_jmap_headers = curl_slist_append(curl_jmap_headers, "Accept: application/json");
    curl_easy_setopt(curl_jmap, CURLOPT_HTTPHEADER, curl_jmap_headers);

    curl_easy_setopt(curl_jmap, CURLOPT_WRITEFUNCTION, _jmap_client_curl_write);
    curl_easy_setopt(curl_jmap, CURLOPT_WRITEDATA, body_in);
  }

  body_out->dptr = body_out->data;
  body_in->dptr = body_in->data;

  _jmap_client_set_url_auth(ctx, curl_jmap);

  json_dump_callback(batch, _jmap_client_json_dump_callback, body_out, JSON_COMPACT); // XXX ret/err

  curl_easy_setopt(curl_jmap, CURLOPT_POSTFIELDS, body_out->data);
  curl_easy_setopt(curl_jmap, CURLOPT_POSTFIELDSIZE, body_out->dptr - body_out->data);

  CURLcode rc = curl_easy_perform(curl_jmap);
  if (rc != CURLE_OK) {
    mutt_debug(1, "jmap: request failed: %s\n", curl_easy_strerror(rc));
    return -1;
  }

  uint32_t code;
  curl_easy_getinfo(curl_jmap, CURLINFO_RESPONSE_CODE, &code);
  if (code != 200) {
    mutt_debug(1, "jmap: HTTP request returned: %d\n", code);
    return -1;
  }

  json_error_t jerr;
  *rbatch = json_loads(body_in->data, 0, &jerr); // XXX ret/err
  if (!*rbatch) {
    mutt_debug(1, "jmap: couldn't parse JMAP response: %s\n", jerr.text);
    return -1;
  }

  return 0;
}
