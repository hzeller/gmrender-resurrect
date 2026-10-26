bin_PROGRAMS = gmediarender

gmediarender_SOURCES = main.c git-version.h \
	upnp_service.c upnp_control.c upnp_connmgr.c  upnp_transport.c \
	upnp_service.h upnp_control.h upnp_connmgr.h  upnp_transport.h \
	song-meta-data.h song-meta-data.c \
	variable-container.h variable-container.c \
	upnp_device.c upnp_device.h \
	upnp_renderer.h upnp_renderer.c \
	webserver.c webserver.h \
	output.c output.h \
	logging.h logging.c \
	xmldoc.c xmldoc.h \
	xmlescape.c xmlescape.h

if HAVE_GST
gmediarender_SOURCES += \
	output_gstreamer.c  output_gstreamer.h
endif

BUILT_SOURCES = git-version.h
EXTRA_DIST = git-version.h

git-version.h: $(wildcard $(top_srcdir)/.git/HEAD $(top_srcdir)/.git/refs/heads/*)
	$(AM_V_GEN)(echo "#define GM_COMPILE_VERSION \"$(shell git describe 2>/dev/null || echo -n '0.3.1')\"" > $@-new; \
	cmp -s $@ $@-new || cp $@-new $@; \
	rm $@-new)

AM_CPPFLAGS = $(GLIB_CFLAGS) $(GST_CFLAGS) $(LIBUPNP_CFLAGS) -DPKG_DATADIR=\"$(datadir)/gmediarender\"
gmediarender_LDADD = $(GLIB_LIBS) $(GST_LIBS) $(LIBUPNP_LIBS)

TESTS = \
	xmlescape_test \
	xmldoc_test \
	song-meta-data_test \
	variable-container_test \
	upnp_service_test \
	upnp_connmgr_test

check_PROGRAMS = $(TESTS)

xmlescape_test_SOURCES = xmlescape_test.c test-helpers.h xmlescape.c xmlescape.h

xmldoc_test_SOURCES = xmldoc_test.c test-helpers.h xmldoc.c xmldoc.h
xmldoc_test_LDADD = $(LIBUPNP_LIBS)

song_meta_data_test_SOURCES = \
	song-meta-data_test.c test-helpers.h \
	song-meta-data.c song-meta-data.h \
	xmldoc.c xmldoc.h \
	xmlescape.c xmlescape.h
song_meta_data_test_LDADD = $(LIBUPNP_LIBS)

variable_container_test_SOURCES = \
	variable-container_test.c test-helpers.h \
	variable-container.c variable-container.h \
	xmldoc.c xmldoc.h \
	xmlescape.c xmlescape.h
variable_container_test_LDADD = $(LIBUPNP_LIBS)

upnp_service_test_SOURCES = \
	upnp_service_test.c test-helpers.h \
	upnp_service.c upnp_service.h \
	variable-container.c variable-container.h \
	upnp_device.c upnp_device.h \
	webserver.c webserver.h \
	logging.c logging.h \
	xmldoc.c xmldoc.h \
	xmlescape.c xmlescape.h
upnp_service_test_LDADD = $(GLIB_LIBS) $(LIBUPNP_LIBS)

upnp_connmgr_test_SOURCES = \
	upnp_connmgr_test.c test-helpers.h \
	upnp_connmgr.c upnp_connmgr.h \
	upnp_service.c upnp_service.h \
	variable-container.c variable-container.h \
	upnp_device.c upnp_device.h \
	webserver.c webserver.h \
	logging.c logging.h \
	xmldoc.c xmldoc.h \
	xmlescape.c xmlescape.h
upnp_connmgr_test_LDADD = $(GLIB_LIBS) $(LIBUPNP_LIBS)

