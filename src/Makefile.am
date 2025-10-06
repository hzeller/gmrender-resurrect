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
	$(AM_V_GEN)(echo "#define GM_COMPILE_VERSION \"$(shell git describe 2>/dev/null || echo -n '0.3')\"" > $@-new; \
	cmp -s $@ $@-new || cp $@-new $@; \
	rm $@-new)

AM_CPPFLAGS = $(GLIB_CFLAGS) $(GST_CFLAGS) $(LIBUPNP_CFLAGS) -DPKG_DATADIR=\"$(datadir)/gmediarender\"
gmediarender_LDADD = $(GLIB_LIBS) $(GST_LIBS) $(LIBUPNP_LIBS)
