/*
 * Copyright (C) 2017 Mehdi Abaakouk <sileht@sileht.net>
 *
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#ifndef _MUTT_TAG_H
#define _MUTT_TAG_H

/**
 * hdr_tag -  Mail Header Tags
 *
 * Keep a linked list of header tags and their transformed values.
 * Textual tags can be transformed to symbols to save space.
 *
 * @sa hdr_tags#tag_list
 */
struct hdr_tag
{
  char *name;
  char *transformed;
  struct hdr_tag *next;
};

/**
 * struct hdr_tags - tags data attached to an email
 *
 * This stores all tags data associated with an email.
 *
 */
struct hdr_tags
{
  /* Without hidden tags */
  char *tags;
  char *tags_transformed;

  /* With hidden tags */
  char *tags_with_hidden;
  struct hdr_tag *tag_list;
};

void hdr_tags_free_tag_list(struct hdr_tag **kw_list);
void hdr_tags_free(HEADER *h);
const char *hdr_tags_get(HEADER *h);
const char *hdr_tags_get_with_hidden(HEADER *h);
const char *hdr_tags_get_transformed(HEADER *h);
const char *hdr_tags_get_transformed_for(char *name, HEADER *h);
void hdr_tags_init(HEADER *h);
void hdr_tags_add(HEADER *h, char *new_tag);
int hdr_tags_replace(HEADER *h, char *tags);

#endif /* _MUTT_TAG_H */
