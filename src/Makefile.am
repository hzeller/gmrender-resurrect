bin_PROGRAMS = gmediarender
lib_LTLIBRARIES = libgmrender_gst.la

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
libgmrender_gst_la_SOURCES = \
	output_gstreamer.c  output_gstreamer.h
endif

main.c : git-version.h

git-version.h: .FORCE
	$(AM_V_GEN)(echo "#define GM_COMPILE_VERSION \"$(shell git log -n1 --date=short --format='0.0.8_git%cd_%h' 2>/dev/null || echo -n '0.0.8')\"" > $@-new; \
	cmp -s $@ $@-new || cp $@-new $@; \
	rm $@-new)

.FORCE:

MOD_MAJOR_VERSION=$(word 1,$(subst ., ,$(GMRENDER_GST_VERSION)))
MOD_MINOR_VERSION=$(word 2,$(subst ., ,$(GMRENDER_GST_VERSION)))
MOD_MICRO_VERSION=$(word 3,$(subst ., ,$(GMRENDER_GST_VERSION)))

AM_CPPFLAGS = $(GLIB_CFLAGS) $(GST_CFLAGS) $(LIBUPNP_CFLAGS) -DPKG_DATADIR=\"$(datadir)/gmediarender\"
gmediarender_CFLAGS = $(GLIB_CFLAGS) $(LIBUPNP_CFLAGS) -DPKG_DATADIR=\"$(datadir)/gmediarender\"
gmediarender_LDADD = $(GLIB_LIBS) $(LIBUPNP_LIBS) -lgmrender_gst
libgmrender_gst_la_CFLAGS = $(GLIB_CFLAGS) $(GST_CFLAGS)
libgmrender_gst_la_CFLAGS += -DMOD_MAJOR_VERSION=$(MOD_MAJOR_VERSION)
libgmrender_gst_la_CFLAGS += -DMOD_MINOR_VERSION=$(MOD_MINOR_VERSION)
libgmrender_gst_la_CFLAGS += -DMOD_MICRO_VERSION=$(MOD_MICRO_VERSION)
libgmrender_gst_la_LIBADD = $(GLIB_LIBS) $(GST_LIBS)
libgmrender_gst_la_LDFLAGS = -release $(GMRENDER_GST_VERSION)
