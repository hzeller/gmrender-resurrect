/* xmldoc.h - XML builder abstraction
 *
 * Copyright (C) 2007   Ivo Clarysse
 *
 * This file is part of GMediaRender.
 *
 * GMediaRender is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GMediaRender is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GMediaRender; if not, write to the Free Software 
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, 
 * MA 02110-1301, USA.
 *
 */

#ifndef _XMLDOC_H
#define _XMLDOC_H

struct xmldoc;
struct xmlelement;

struct xmldoc *xmldoc_new(void);

int xmldoc_free(struct xmldoc *doc);
char *xmldoc_tostring(struct xmldoc *doc);

struct xmlelement *xmldoc_new_topelement(struct xmldoc *doc,
                                         const char *elementName,
                                         const char *xmlns);

struct xmlelement *xmlelement_new(struct xmldoc *doc, const char *elementName);

int xmlelement_free(struct xmlelement *element);
int xmlelement_add_element(struct xmldoc *doc,
                           struct xmlelement *parent,
                           struct xmlelement *child);
int xmlelement_add_text(struct xmldoc *doc,
                        struct xmlelement *parent,
                        const char *text);
int xmlelement_set_attribute(struct xmldoc *doc,
                             struct xmlelement *element,
                             const char *name,
                             const char *value);

void add_value_element(struct xmldoc *doc,
                       struct xmlelement *parent,
                       const char *tagname, const char *value);
void add_value_element_int(struct xmldoc *doc,
                           struct xmlelement *parent,
                           const char *tagname, int value);
void add_value_element_long(struct xmldoc *doc,
                            struct xmlelement *parent,
                            const char *tagname, long long value);



#endif /* _XMLDOC_H */
