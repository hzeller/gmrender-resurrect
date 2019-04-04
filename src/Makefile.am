bin_PROGRAMS = gmediarender

gmediarender_SOURCES = main.c git-version.h \
	output_module.c output_module.h \
	upnp.c upnp_control.c upnp_connmgr.c  upnp_transport.c \
	upnp.h upnp_control.h upnp_connmgr.h  upnp_transport.h \
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

if HAVE_MPG123
gmediarender_SOURCES += \
	output_mpg123.c  output_mpg123.h \
	sound_module.c sound_module.h \
	webclient.c webclient.h

if HAVE_ALSA
gmediarender_SOURCES += \
	sound_alsa.c
endif
endif

main.c : git-version.h

git-version.h: .FORCE
	$(AM_V_GEN)(echo "#define GM_COMPILE_VERSION \"$(shell git log -n1 --date=short --format='%cd_%h' 2>/dev/null || echo -n '0.0.7-git')\"" > $@-new; \
	cmp -s $@ $@-new || cp $@-new $@; \
	rm $@-new)

.FORCE:

AM_CPPFLAGS = $(GLIB_CFLAGS) $(GST_CFLAGS) $(LIBUPNP_CFLAGS) $(MPG123_CFLAGS) $(ALSA_CFLAGS) -DPKG_DATADIR=\"$(datadir)/gmediarender\"
gmediarender_LDADD = $(GLIB_LIBS) $(GST_LIBS) $(LIBUPNP_LIBS) $(MPG123_LIBS) $(ALSA_LIBS)
