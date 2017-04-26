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

jmap_context_t *jmap_context_new(void)
{
  jmap_context_t *jctx = safe_calloc(sizeof(jmap_context_t));
  return jctx;
}

void jmap_context_free(jmap_context_t **jctxp) {
  jmap_context_t *jctx = *jctxp;
  FREE(jctxp);
}
