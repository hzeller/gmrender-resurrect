/* song-meta-data - Object holding meta data for a song.
 *
 * Copyright (C) 2012 Henner Zeller
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

// TODO: we're assuming that the namespaces are abbreviated with 'dc' and 'upnp'
// ... but if I understand that correctly, that doesn't need to be the case.

#include "song-meta-data.h"

#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "xmlescape.h"
#include "xmldoc.h"

void SongMetaData_init(struct SongMetaData *value) {
	memset(value, 0, sizeof(struct SongMetaData));
}
void SongMetaData_clear(struct SongMetaData *value) {
	free((char*)value->title);
	value->title = NULL;
	free((char*)value->artist);
	value->artist = NULL;
	free((char*)value->album);
	value->album = NULL;
	free((char*)value->genre);
	value->genre = NULL;
}

static const char kDidlHeader[] = "<DIDL-Lite "
	"xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" "
	"xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
	"xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\">";
static const char kDidlFooter[] = "</DIDL-Lite>";

// Allocates a new DIDL formatted XML and fill it with given data.
// The input fields are expected to be already xml escaped.
static char *generate_DIDL(const char *id,
			   const char *title, const char *artist,
			   const char *album, const char *genre,
			   const char *composer) {
	char *result = NULL;
	int ret = asprintf(&result, "%s\n<item id=\"%s\">\n"
			  "\t<dc:title>%s</dc:title>\n"
			  "\t<upnp:artist>%s</upnp:artist>\n"
			  "\t<upnp:album>%s</upnp:album>\n"
			  "\t<upnp:genre>%s</upnp:genre>\n"
			  "\t<upnp:creator>%s</upnp:creator>\n"
			  "</item>\n%s",
			  kDidlHeader, id,
			  title ? title : "", artist ? artist : "",
			  album ? album : "", genre ? genre : "",
			  composer ? composer : "",
			  kDidlFooter);
	return ret >= 0 ? result : NULL;
}

// Takes input, if it finds the given tag, then replaces the content between
// these with 'content'. It might re-allocate the original string; only the
// returned string is valid.
// updates "edit_count" if there was a change.
// Very crude way to edit XML.
static char *replace_range(char *input,
			   const char *tag_start, const char *tag_end,
			   const char *content,
			   int *edit_count) {
	if (content == NULL)  // unknown content; document unchanged.
		return input;
	const int total_len = strlen(input);
	const char *start_pos = strstr(input, tag_start);
	if (start_pos == NULL) return input;
	start_pos += strlen(tag_start);
	const char *end_pos = strstr(start_pos, tag_end);
	if (end_pos == NULL) return input;
	int old_content_len = end_pos - start_pos;
	int new_content_len = strlen(content);
	char *result = NULL;
	if (old_content_len != new_content_len) {
		result = (char*)malloc(total_len
				       + new_content_len - old_content_len + 1);
		strncpy(result, input, start_pos - input);
		strncpy(result + (start_pos - input), content, new_content_len);
		strcpy(result + (start_pos - input) + new_content_len, end_pos);
		free(input);
		++*edit_count;
	} else {
		// Typically, we replace the same content with itself - same
		// length. No realloc in this case.
		if (strncmp(start_pos, content, new_content_len) != 0) {
			const int offset = start_pos - input;
			strncpy(input + offset, content, new_content_len);
			++*edit_count;
		}
		result = input;
	}
	return result;
}

int SongMetaData_parse_DIDL(struct SongMetaData *object, const char *xml) {
	struct xmldoc *doc = xmldoc_parsexml(xml);
	if (doc == NULL)
		return 0;

	// ... did I mention that I hate navigating XML documents ?
	struct xmlelement *didl_node = find_element_in_doc(doc, "DIDL-Lite");
	if (didl_node == NULL)
		return 0;

	struct xmlelement *item_node = find_element_in_element(didl_node,
							       "item");
	if (item_node == NULL)
		return 0;

	struct xmlelement *value_node = NULL;
	value_node = find_element_in_element(item_node, "dc:title");
	if (value_node) object->title = get_node_value(value_node);

	value_node = find_element_in_element(item_node, "upnp:artist");
	if (value_node) object->artist = get_node_value(value_node);

	value_node = find_element_in_element(item_node, "upnp:album");
	if (value_node) object->album = get_node_value(value_node);

	value_node = find_element_in_element(item_node, "upnp:genre");
	if (value_node) object->genre = get_node_value(value_node);

	xmldoc_free(doc);
	return 1;
}

// TODO: actually use some XML library for this, but spending too much time
// with XML is not good for the brain :) Worst thing that came out of the 90ies.
char *SongMetaData_to_DIDL(const struct SongMetaData *object,
			   const char *original_xml) {
	// Generating a unique ID in case the players cache the content by
	// the item-ID. Right now this is experimental and not known to make
	// any difference - it seems that players just don't display changes
	// in the input stream. Grmbl.
	static unsigned int xml_id = 42;
	char unique_id[4 + 8 + 1];
	snprintf(unique_id, sizeof(unique_id), "gmr-%08x", xml_id++);

	char *result;
	char *title, *artist, *album, *genre, *composer;
	title = object->title ? xmlescape(object->title, 0) : NULL;
	artist = object->artist ? xmlescape(object->artist, 0) : NULL;
	album = object->album ? xmlescape(object->album, 0) : NULL;
	genre = object->genre ? xmlescape(object->genre, 0) : NULL;
	composer = object->composer ? xmlescape(object->composer, 0) : NULL;
	if (original_xml == NULL || strlen(original_xml) == 0) {
		result = generate_DIDL(unique_id, title, artist, album,
				       genre, composer);
	} else {
		int edits = 0;
		// Otherwise, surgically edit the original document to give
		// control points as close as possible what they sent themself.
		result = strdup(original_xml);
		result = replace_range(result, "<dc:title>", "</dc:title>",
				       title, &edits);
		result = replace_range(result,
				       "<upnp:artist>", "</upnp:artist>",
				       artist, &edits);
		result = replace_range(result, "<upnp:album>", "</upnp:album>",
				       album, &edits);
		result = replace_range(result, "<upnp:genre>", "</upnp:genre>",
				       genre, &edits);
		result = replace_range(result,
				       "<upnp:creator>", "</upnp:creator>",
				       composer, &edits);
		if (edits) {
			// Only if we changed the content, we generate a new
			// unique id.
			result = replace_range(result, "id=\"", "\"", unique_id,
					       &edits);
		}
	}
	free(title);
	free(artist);
	free(album);
	free(genre);
	free(composer);
	return result;
}
