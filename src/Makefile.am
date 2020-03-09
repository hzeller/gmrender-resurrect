bin_PROGRAMS = gmediarender
BUILT_SOURCES =
CLEANFILES =

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

if ENABLE_MPRIS
mpris_TARGETS = mpris_interface.c mpris_interface.h

$(mpris_TARGETS):  mpris-spec/*.xml
	gdbus-codegen --interface-prefix org.mpris --c-namespace Mpris --generate-c-code mpris_interface $^

gmediarender_SOURCES += \
	mpris_notification.c mpris_notification.h \
	$(mpris_TARGETS)

BUILT_SOURCES += $(mpris_TARGETS)
CLEANFILES += $(mpris_TARGETS)
endif

main.c : git-version.h

git-version.h: .FORCE
	$(AM_V_GEN)(echo "#define GM_COMPILE_VERSION \"$(shell git log -n1 --date=short --format='0.1_git%cd_%h' 2>/dev/null || echo -n '0.1')\"" > $@-new; \
	cmp -s $@ $@-new || cp $@-new $@; \
	rm $@-new)

.FORCE:

AM_CPPFLAGS = $(GLIB_CFLAGS) $(GST_CFLAGS) $(LIBUPNP_CFLAGS) -DPKG_DATADIR=\"$(datadir)/gmediarender\"
gmediarender_LDADD = $(GLIB_LIBS) $(GST_LIBS) $(LIBUPNP_LIBS)

install-exec-hook:
if ENABLE_MPRIS
DBUS_POLICY_DIR=$(DESTDIR)/etc/dbus-1/system.d
DBUS_POLICY_TARGET=$(DBUS_POLICY_DIR)/gmrender-resurrect-mpris-policy.conf
	mkdir -p $(DBUS_POLICY_DIR)
	[ -f $(DBUS_POLICY_TARGET) ] || cp ../scripts/gmrender-resurrect-mpris-policy.conf $(DBUS_POLICY_TARGET)
endif
