bin_PROGRAMS = gmediarender

gmediarender_SOURCES = main.cc git-version.h \
	upnp_service.cc upnp_control.cc upnp_connmgr.cc  upnp_transport.cc \
	upnp_service.h upnp_control.h upnp_connmgr.h  upnp_transport.h \
	song-meta-data.h song-meta-data.cc \
	variable-container.h variable-container.cc \
	upnp_device.cc upnp_device.h \
	upnp_renderer.h upnp_renderer.cc \
	webserver.cc webserver.h \
	output.cc output.h \
	logging.h logging.cc \
	xmldoc.cc xmldoc.h \
	xmlescape.cc xmlescape.h

if HAVE_GST
gmediarender_SOURCES += \
	output_gstreamer.cc  output_gstreamer.h
endif

main.cc : git-version.h

git-version.h: .FORCE
	$(AM_V_GEN)(echo "#define GM_COMPILE_VERSION \"$(shell git log -n1 --date=short --format='0.0.8_git%cd_%h' 2>/dev/null || echo -n '0.0.8')\"" > $@-new; \
	cmp -s $@ $@-new || cp $@-new $@; \
	rm $@-new)

.FORCE:

AM_CPPFLAGS = $(GLIB_CFLAGS) $(GST_CFLAGS) $(LIBUPNP_CFLAGS) -DPKG_DATADIR=\"$(datadir)/gmediarender\"
gmediarender_LDADD = $(GLIB_LIBS) $(GST_LIBS) $(LIBUPNP_LIBS)
