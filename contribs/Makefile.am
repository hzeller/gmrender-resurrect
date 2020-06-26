if HAVE_MPG123
pkglib_LTLIBRARIES = gmrender_mpg123.la
endif

gmrender_mpg123_la_SOURCES = \
	sound_module.c sound_module.h \
	sound_alsa.c \
	webclient.c \
	output_mpg123.c

GMRENDER_MPG123_VERSION=0.0.1

MOD_MAJOR_VERSION=$(word 1,$(subst ., ,$(GMRENDER_MPG123_VERSION)))
MOD_MINOR_VERSION=$(word 2,$(subst ., ,$(GMRENDER_MPG123_VERSION)))
MOD_MICRO_VERSION=$(word 3,$(subst ., ,$(GMRENDER_MPG123_VERSION)))

gmrender_mpg123_la_CFLAGS = $(GLIB_CFLAGS) $(ALSA_CFLAGS) -I ../src
gmrender_mpg123_la_CFLAGS += -DMOD_MAJOR_VERSION=$(MOD_MAJOR_VERSION)
gmrender_mpg123_la_CFLAGS += -DMOD_MINOR_VERSION=$(MOD_MINOR_VERSION)
gmrender_mpg123_la_CFLAGS += -DMOD_MICRO_VERSION=$(MOD_MICRO_VERSION)
gmrender_mpg123_la_LIBADD = $(GLIB_LIBS) $(ALSA_LIBS) -lmpg123
gmrender_mpg123_la_LDFLAGS = -module -release $(GMRENDER_MPG123_VERSION)
