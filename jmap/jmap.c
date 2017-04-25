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

struct mx_ops mx_jmap_ops = {
  .open         = jmap_mailbox_open,
  .open_append  = jmap_mailbox_open_append,
  .close        = jmap_mailbox_close,
  .open_msg     = jmap_message_open,
  .close_msg    = jmap_message_close,
  .commit_msg   = jmap_message_commit,
  .open_new_msg = jmap_message_open_new,
  .check        = jmap_mailbox_check,
  .sync         = jmap_mailbox_sync,
};
