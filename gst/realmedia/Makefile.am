plugin_LTLIBRARIES = libgstrmdemux.la

libgstrmdemux_la_SOURCES = rademux.c rmdemux.c  \
			   rmutils.c rdtdepay.c rdtmanager.c \
			   rtspreal.c realhash.c asmrules.c \
			   rdtjitterbuffer.c gstrdtbuffer.c \
			   pnmsrc.c realmedia.c


libgstrmdemux_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstrmdemux_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
				-lgstrtsp-@GST_MAJORMINOR@ \
				-lgstsdp-@GST_MAJORMINOR@ \
				-lgstpbutils-@GST_MAJORMINOR@ \
				$(GST_BASE_LIBS) $(GST_LIBS)
libgstrmdemux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrmdemux_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = rademux.h rmdemux.h rmutils.h rdtdepay.h rdtmanager.h \
		 rdtjitterbuffer.h rtspreal.h realhash.h asmrules.h gstrdtbuffer.h \
		 pnmsrc.h

noinst_PROGRAMS = asmrules
asmrules_CFLAGS = $(GST_CFLAGS) -DTEST
asmrules_LDADD = $(GST_LIBS) $(LIBM)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstrmdemux -:SHARED libgstrmdemux \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstrmdemux_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstrmdemux_la_CFLAGS) \
	 -:LDFLAGS $(libgstrmdemux_la_LDFLAGS) \
	           $(libgstrmdemux_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
