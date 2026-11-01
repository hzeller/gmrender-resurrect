/* song-meta-data_test.c - Unit tests for song-meta-data
 *
 * This file is part of GMediaRender.
 */

#include "test-helpers.h"
#include "song-meta-data.h"

static void test_init_and_clear(void) {
	struct SongMetaData meta;
	SongMetaData_init(&meta);
	EXPECT_TRUE(meta.title == NULL);
	EXPECT_TRUE(meta.artist == NULL);
	EXPECT_TRUE(meta.album == NULL);
	EXPECT_TRUE(meta.genre == NULL);
	EXPECT_TRUE(meta.composer == NULL);
	EXPECT_TRUE(meta.album_art_uri == NULL);

	meta.title = strdup("Test Title");
	meta.artist = strdup("Test Artist");
	meta.album = strdup("Test Album");
	meta.genre = strdup("Test Genre");
	meta.composer = strdup("Test Composer");
	meta.album_art_uri = strdup("http://example.com/art.jpg");

	SongMetaData_clear(&meta);
	EXPECT_TRUE(meta.title == NULL);
	EXPECT_TRUE(meta.artist == NULL);
	EXPECT_TRUE(meta.album == NULL);
	EXPECT_TRUE(meta.genre == NULL);
	EXPECT_TRUE(meta.composer == NULL);
	EXPECT_TRUE(meta.album_art_uri == NULL);
}

static void test_generate_and_parse_didl_roundtrip(void) {
	struct SongMetaData src;
	SongMetaData_init(&src);
	src.title = "Song <1> & More";
	src.artist = "Artist & Friends";
	src.album = "Album \"Best Of\"";
	src.genre = "Rock/Pop";
	src.composer = "J.S. Bach";
	src.album_art_uri = "http://example.com/cover.jpg?a=1&b=2";

	char *didl = SongMetaData_to_DIDL(&src, NULL);
	EXPECT_TRUE(didl != NULL);
	/* Ensure XML escaping happened in the DIDL string. */
	EXPECT_TRUE(strstr(didl, "<dc:title>Song &lt;1&gt; &amp; More</dc:title>") != NULL);
	EXPECT_TRUE(strstr(didl, "<upnp:artist>Artist &amp; Friends</upnp:artist>") != NULL);
	EXPECT_TRUE(strstr(didl, "<upnp:creator>J.S. Bach</upnp:creator>") != NULL);
	EXPECT_TRUE(strstr(didl, "<upnp:albumArtURI>http://example.com/cover.jpg?a=1&amp;b=2</upnp:albumArtURI>") != NULL);

	struct SongMetaData parsed;
	SongMetaData_init(&parsed);
	int ok = SongMetaData_parse_DIDL(&parsed, didl);
	EXPECT_TRUE(ok == 1);
	/* Parsing again into the same struct should not leak previous strings. */
	ok = SongMetaData_parse_DIDL(&parsed, didl);
	EXPECT_TRUE(ok == 1);
	EXPECT_STREQ(parsed.title, "Song <1> & More");
	EXPECT_STREQ(parsed.artist, "Artist & Friends");
	EXPECT_STREQ(parsed.album, "Album \"Best Of\"");
	EXPECT_STREQ(parsed.genre, "Rock/Pop");
	EXPECT_STREQ(parsed.album_art_uri, "http://example.com/cover.jpg?a=1&b=2");

	SongMetaData_clear(&parsed);
	free(didl);
}

static void test_surgical_xml_editing(void) {
	const char *orig_xml =
		"<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" "
		"xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
		"xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\">"
		"<item id=\"orig-id\" parentID=\"0\" restricted=\"1\">"
		"<dc:title>Old Title</dc:title>"
		"<upnp:artist>Old Artist</upnp:artist>"
		"<upnp:album>Old Album</upnp:album>"
		"<res protocolInfo=\"http-get:*:audio/mpeg:*\">http://example.com/1.mp3</res>"
		"</item></DIDL-Lite>";

	/* 1. No changes (all fields NULL or identical, or empty album_art_uri)
	 * -> id stays "orig-id" and no <upnp:albumArtURI> is injected. */
	struct SongMetaData unchanged;
	SongMetaData_init(&unchanged);
	unchanged.title = "Old Title";
	unchanged.album_art_uri = "";
	char *res_same = SongMetaData_to_DIDL(&unchanged, orig_xml);
	EXPECT_STREQ(res_same, orig_xml);
	EXPECT_TRUE(strstr(res_same, "<upnp:albumArtURI>") == NULL);
	free(res_same);

	/* 2. Updating title and artist -> replaces tag contents, preserves <res>,
	 * does not add <upnp:albumArtURI> when NULL, and updates item id="...". */
	struct SongMetaData updated;
	SongMetaData_init(&updated);
	updated.title = "New Longer Title & Stuff";
	updated.artist = "Short";
	char *res_edited = SongMetaData_to_DIDL(&updated, orig_xml);
	EXPECT_TRUE(strstr(res_edited, "<dc:title>New Longer Title &amp; Stuff</dc:title>") != NULL);
	EXPECT_TRUE(strstr(res_edited, "<upnp:artist>Short</upnp:artist>") != NULL);
	EXPECT_TRUE(strstr(res_edited, "<upnp:album>Old Album</upnp:album>") != NULL);
	EXPECT_TRUE(strstr(res_edited, "<res protocolInfo=\"http-get:*:audio/mpeg:*\">") != NULL);
	EXPECT_TRUE(strstr(res_edited, "<upnp:albumArtURI>") == NULL);
	EXPECT_TRUE(strstr(res_edited, "id=\"orig-id\"") == NULL);
	EXPECT_TRUE(strstr(res_edited, "id=\"gmr-") != NULL);
	free(res_edited);

	/* 3. Setting album_art_uri when original XML has no <upnp:albumArtURI>
	 * -> inserts <upnp:albumArtURI> before </item> and updates item id. */
	struct SongMetaData with_art;
	SongMetaData_init(&with_art);
	with_art.album_art_uri = "http://example.com/art.png?x=1&y=2";
	char *res_added_art = SongMetaData_to_DIDL(&with_art, orig_xml);
	const char *art_tag = strstr(
		res_added_art,
		"<upnp:albumArtURI>http://example.com/art.png?x=1&amp;y=2</upnp:albumArtURI>");
	const char *item_end = strstr(res_added_art, "</item>");
	EXPECT_TRUE(art_tag != NULL);
	EXPECT_TRUE(item_end != NULL && art_tag < item_end);
	EXPECT_TRUE(strstr(res_added_art, "id=\"orig-id\"") == NULL);

	/* 4. Updating album_art_uri when XML already has <upnp:albumArtURI>
	 * -> replaces existing tag content without inserting a duplicate tag. */
	with_art.album_art_uri = "http://example.com/new-art.png";
	char *res_replaced_art = SongMetaData_to_DIDL(&with_art, res_added_art);
	const char *first_art = strstr(
		res_replaced_art,
		"<upnp:albumArtURI>http://example.com/new-art.png</upnp:albumArtURI>");
	EXPECT_TRUE(first_art != NULL);
	EXPECT_TRUE(strstr(first_art + 1, "<upnp:albumArtURI>") == NULL);

	free(res_replaced_art);
	free(res_added_art);
}

static void test_parse_invalid_didl(void) {
	struct SongMetaData meta;
	SongMetaData_init(&meta);

	EXPECT_TRUE(SongMetaData_parse_DIDL(&meta, "not xml") == 0);
	EXPECT_TRUE(SongMetaData_parse_DIDL(&meta, "<other></other>") == 0);
	EXPECT_TRUE(SongMetaData_parse_DIDL(&meta, "<DIDL-Lite></DIDL-Lite>") == 0);
}

int main(void) {
	test_init_and_clear();
	test_generate_and_parse_didl_roundtrip();
	test_surgical_xml_editing();
	test_parse_invalid_didl();
	return 0;
}
