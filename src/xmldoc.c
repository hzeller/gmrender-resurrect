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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#ifdef HAVE_LIBUPNP
#include <upnp/ixml.h>
#endif

#include "xmldoc.h"

struct xmldoc {
#ifdef HAVE_LIBUPNP
	IXML_Document *doc;
#endif
};
struct xmlelement {
#ifdef HAVE_LIBUPNP
	IXML_Element *element;
#endif
};

struct xmldoc *xmldoc_new(void)
{
	struct xmldoc *result = NULL;
	result = malloc(sizeof(*result));
#ifdef HAVE_LIBUPNP
	IXML_Document *doc;
	doc = ixmlDocument_createDocument();
	result->doc = doc;
#endif
	return result;
}

int xmldoc_free(struct xmldoc *doc)
{
	int result = -1;
	assert(doc != NULL);
#ifdef HAVE_LIBUPNP
	ixmlDocument_free(doc->doc);
#endif
	free(doc);
	result = 0;
	return result;
}

char *xmldoc_tostring(struct xmldoc *doc)
{
	char *result = NULL;
	assert(doc != NULL);
#ifdef HAVE_LIBUPNP
	result = ixmlDocumenttoString(doc->doc);
#endif
	return result;
}

struct xmlelement *xmldoc_new_topelement(struct xmldoc *doc,
                                         const char *elementName,
                                         const char *xmlns)
{
	struct xmlelement *result = NULL;
	assert(doc != NULL);
	assert(elementName != NULL);
	result = malloc(sizeof(*result));
#ifdef HAVE_LIBUPNP
	IXML_Element *element;
	if (xmlns) {
		element = ixmlDocument_createElementNS(doc->doc, xmlns,
                                                       elementName);
                ixmlElement_setAttribute(element, "xmlns",
                                         xmlns);
	} else {
		element = ixmlDocument_createElement(doc->doc, elementName);
	}
	ixmlNode_appendChild((IXML_Node *)(doc->doc),(IXML_Node *)element);
	result->element = element;
#endif
	return result;
}

struct xmlelement *xmlelement_new(struct xmldoc *doc, const char *elementName)
{
	struct xmlelement *result = NULL;
	assert(doc != NULL);
	assert(elementName != NULL);
	result = malloc(sizeof(*result));
#ifdef HAVE_LIBUPNP
	IXML_Element *element;
	element = ixmlDocument_createElement(doc->doc, elementName);
	result->element = element;
#endif
	return result;
}

int xmlelement_free(struct xmlelement *element)
{
	int result = -1;
	assert(element != NULL);
	return result;
}

int xmlelement_add_element(struct xmldoc *doc,
                           struct xmlelement *parent,
                           struct xmlelement *child)
{
	int result = -1;
	assert(doc != NULL);
	assert(parent != NULL);
	assert(child != NULL);
#ifdef HAVE_LIBUPNP
	ixmlNode_appendChild((IXML_Node *)(parent->element),
                             (IXML_Node *)(child->element));
	result = 0;
#endif
	return result;
}

int xmlelement_add_text(struct xmldoc *doc,
                        struct xmlelement *parent,
                        const char *text)
{
	int result = -1;
	assert(doc != NULL);
	assert(parent != NULL);
	assert(text != NULL);
#ifdef HAVE_LIBUPNP
	IXML_Node *textNode;
	textNode = ixmlDocument_createTextNode(doc->doc, text);
	ixmlNode_appendChild((IXML_Node *)(parent->element),
                             textNode);
	result = 0;
#endif
	return result;
}
int xmlelement_set_attribute(struct xmldoc *doc,
                             struct xmlelement *element,
                             const char *name,
                             const char *value)
{
	int result = -1;
	assert(doc != NULL);
	assert(element != NULL);
	assert(name != NULL);
	assert(value != NULL);
#ifdef HAVE_LIBUPNP
	ixmlElement_setAttribute(element->element, name, value);
	result = 0;
#endif
	return result;
}

void add_value_element(struct xmldoc *doc,
                       struct xmlelement *parent,
                       const char *tagname, const char *value)
{
        struct xmlelement *top;

        top=xmlelement_new(doc, tagname);
        xmlelement_add_text(doc, top, value);
        xmlelement_add_element(doc, parent, top);
}
void add_value_element_int(struct xmldoc *doc,
                           struct xmlelement *parent,
                           const char *tagname, int value)
{
        char *buf;

        asprintf(&buf,"%d",value);
        add_value_element(doc, parent, tagname, buf);
        free(buf);
}
void add_value_element_long(struct xmldoc *doc,
                            struct xmlelement *parent,
                            const char *tagname, long long value)
{
        char *buf;

        asprintf(&buf,"%lld",value);
        add_value_element(doc, parent, tagname, buf);
        free(buf);
}

