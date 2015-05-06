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
#include <string.h>

#include <upnp/ixml.h>

#include "xmldoc.h"

// The structs are mere placeholders that we internally cast to the
// objects used in the upnplib.
struct xmlelement {};
struct xmldoc {};

static IXML_Document *to_idoc(struct xmldoc *x) {
	return (IXML_Document*) x;
}

static IXML_Element *to_ielem(struct xmlelement *x) {
	return (IXML_Element*) x;
}

struct xmldoc *xmldoc_new(void)
{
	IXML_Document *doc;
	doc = ixmlDocument_createDocument();
	return (struct xmldoc*) doc;
}

void xmldoc_free(struct xmldoc *doc)
{
	assert(doc != NULL);
	ixmlDocument_free(to_idoc(doc));
}

char *xmldoc_tostring(struct xmldoc *doc)
{
	char *result = NULL;
	assert(doc != NULL);
	result = ixmlDocumenttoString(to_idoc(doc));
	return result;
}

struct xmldoc *xmldoc_parsexml(const char *xml_text) {
	IXML_Document *doc = ixmlParseBuffer(xml_text);
	return (struct xmldoc*) doc;
}

struct xmlelement *xmldoc_new_topelement(struct xmldoc *doc,
                                         const char *elementName,
                                         const char *xmlns)
{
	assert(doc != NULL);
	assert(elementName != NULL);
	IXML_Element *element;
	if (xmlns) {
		element = ixmlDocument_createElementNS(to_idoc(doc), xmlns,
                                                       elementName);
                ixmlElement_setAttribute(element, "xmlns", xmlns);
	} else {
		element = ixmlDocument_createElement(to_idoc(doc), elementName);
	}
	ixmlNode_appendChild((IXML_Node *)(to_idoc(doc)),(IXML_Node *)element);
	return (struct xmlelement *) element;
}

struct xmlelement *xmlelement_new(struct xmldoc *doc, const char *elementName)
{
	assert(doc != NULL);
	assert(elementName != NULL);
	IXML_Element *element;
	element = ixmlDocument_createElement(to_idoc(doc), elementName);
	return (struct xmlelement*) element;
}

static struct xmlelement *find_element(IXML_Node *node, const char *key) {
	node = ixmlNode_getFirstChild(node);
	for (/**/; node != NULL; node = ixmlNode_getNextSibling(node)) {
		if (strcmp(ixmlNode_getNodeName(node), key) == 0) {
			return (struct xmlelement*) node;
		}
	}
	return NULL;
}

struct xmlelement *find_element_in_doc(struct xmldoc *doc,
				       const char *key) {

	return find_element((IXML_Node*) to_idoc(doc), key);
}

struct xmlelement *find_element_in_element(struct xmlelement *element,
					   const char *key) {
	return find_element((IXML_Node*) to_ielem(element), key);
}

char *get_node_value(struct xmlelement *element) {
	IXML_Node *node = (IXML_Node*) to_ielem(element);
	node = ixmlNode_getFirstChild(node);
	const char *node_value = (node != NULL
				  ? ixmlNode_getNodeValue(node)
				  : NULL);
	return strdup(node_value != NULL ? node_value : "");
}

void xmlelement_add_element(struct xmldoc *doc,
			    struct xmlelement *parent,
			    struct xmlelement *child)
{
	assert(doc != NULL);
	assert(parent != NULL);
	assert(child != NULL);
	ixmlNode_appendChild((IXML_Node *) to_ielem(parent),
                             (IXML_Node *) to_ielem(child));
}

void xmlelement_add_text(struct xmldoc *doc,
			 struct xmlelement *parent,
			 const char *text)
{
	assert(doc != NULL);
	assert(parent != NULL);
	assert(text != NULL);
	IXML_Node *textNode;
	textNode = ixmlDocument_createTextNode(to_idoc(doc), text);
	ixmlNode_appendChild((IXML_Node *) to_ielem(parent), textNode);
}

void xmlelement_set_attribute(struct xmldoc *doc,
			      struct xmlelement *element,
			      const char *name,
			      const char *value)
{
	assert(doc != NULL);
	assert(element != NULL);
	assert(name != NULL);
	assert(value != NULL);
	ixmlElement_setAttribute(to_ielem(element), name, value);
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

struct xmlelement *add_attributevalue_element(struct xmldoc *doc,
					      struct xmlelement *parent,
					      const char *tagname,
					      const char *attribute_name,
					      const char *value)
{
        struct xmlelement *top;

        top = xmlelement_new(doc, tagname);
        xmlelement_set_attribute(doc, top, attribute_name, value);
        xmlelement_add_element(doc, parent, top);
	return top;
}

void add_value_element_int(struct xmldoc *doc,
                           struct xmlelement *parent,
                           const char *tagname, int value)
{
        char *buf;

        if (asprintf(&buf,"%d",value) >= 0) {
		add_value_element(doc, parent, tagname, buf);
		free(buf);
	}
}
void add_value_element_long(struct xmldoc *doc,
                            struct xmlelement *parent,
                            const char *tagname, long long value)
{
        char *buf;

        if (asprintf(&buf,"%lld",value) >= 0) {
		add_value_element(doc, parent, tagname, buf);
		free(buf);
	}
}

