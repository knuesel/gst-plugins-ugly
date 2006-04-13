include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.xml

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
	$(REGISTRY_ENVIRONMENT)					\
	GST_PLUGIN_SYSTEM_PATH=					\
	GST_PLUGIN_PATH=$(top_builddir)/gst:$(top_builddir)/ext:$(GST_PLUGINS_DIR)

# ths core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.xml

clean-local: clean-local-check

TESTS = $(check_PROGRAMS)

if USE_AMRNB
AMRNB = elements/amrnbenc
else
AMRNB =
endif

check_PROGRAMS = \
	$(AMRNB)

# these tests don't even pass
noinst_PROGRAMS =

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(GST_CHECK_CFLAGS) $(CHECK_CFLAGS)
LDADD = $(GST_OBJ_LIBS) $(GST_CHECK_LIBS) $(CHECK_LIBS)

# valgrind testing
VALGRIND_TESTS_DISABLE =

SUPPRESSIONS = $(top_srcdir)/common/gst.supp $(srcdir)/gst-plugins-ugly.supp

elements_cmmldec_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS)
elements_cmmlenc_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS)

EXTRA_DIST = gst-plugins-ugly.supp