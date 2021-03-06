/**
 * Copyright (C) 2000-2003 Vsevolod Volkov <vvv@mutt.org.ua>
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

#ifndef _MUTT_POP_H
#define _MUTT_POP_H 1

#include "bcache.h"
#include "mailbox.h"
#include "mutt_curses.h"
#include "mutt_socket.h"

#define POP_PORT 110
#define POP_SSL_PORT 995

/* number of entries in the hash table */
#define POP_CACHE_LEN 10

/* maximal length of the server response (RFC1939) */
#define POP_CMD_RESPONSE 512

enum
{
  /* Status */
  POP_NONE = 0,
  POP_CONNECTED,
  POP_DISCONNECTED,
  POP_BYE
};

typedef enum {
  POP_A_SUCCESS = 0,
  POP_A_SOCKET,
  POP_A_FAILURE,
  POP_A_UNAVAIL
} pop_auth_res_t;

typedef struct
{
  unsigned int index;
  char *path;
} POP_CACHE;

typedef struct
{
  CONNECTION *conn;
  unsigned int status : 2;
  bool capabilities : 1;
  unsigned int use_stls : 2;
  bool cmd_capa : 1;         /* optional command CAPA */
  bool cmd_stls : 1;         /* optional command STLS */
  unsigned int cmd_user : 2; /* optional command USER */
  unsigned int cmd_uidl : 2; /* optional command UIDL */
  unsigned int cmd_top : 2;  /* optional command TOP */
  bool resp_codes : 1;       /* server supports extended response codes */
  bool expire : 1;           /* expire is greater than 0 */
  bool clear_cache : 1;
  size_t size;
  time_t check_time;
  time_t login_delay; /* minimal login delay  capability */
  char *auth_list;    /* list of auth mechanisms */
  char *timestamp;
  body_cache_t *bcache; /* body cache */
  char err_msg[POP_CMD_RESPONSE];
  POP_CACHE cache[POP_CACHE_LEN];
} POP_DATA;

typedef struct
{
  /* do authentication, using named method or any available if method is NULL */
  pop_auth_res_t (*authenticate)(POP_DATA *, const char *);
  /* name of authentication method supported, NULL means variable. If this
   * is not null, authenticate may ignore the second parameter. */
  const char *method;
} pop_auth_t;

/* pop_auth.c */
int pop_authenticate(POP_DATA *pop_data);
void pop_apop_timestamp(POP_DATA *pop_data, char *buf);

/* pop_lib.c */
#define pop_query(A, B, C) pop_query_d(A, B, C, NULL)
int pop_parse_path(const char *path, ACCOUNT *acct);
int pop_connect(POP_DATA *pop_data);
int pop_open_connection(POP_DATA *pop_data);
int pop_query_d(POP_DATA *pop_data, char *buf, size_t buflen, char *msg);
int pop_fetch_data(POP_DATA *pop_data, char *query, progress_t *progressbar,
                   int (*funct)(char *, void *), void *data);
int pop_reconnect(CONTEXT *ctx);
void pop_logout(CONTEXT *ctx);

/* pop.c */
void pop_fetch_mail(void);

extern struct mx_ops mx_pop_ops;

#endif /* _MUTT_POP_H */
