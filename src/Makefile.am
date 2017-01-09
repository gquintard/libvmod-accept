AM_CFLAGS = $(VARNISHAPI_CFLAGS) -Wall -Werror
AM_LDFLAGS = $(VARNISHAPI_LIBS) $(VMOD_LDFLAGS) $(CODE_COVERAGE_LDFLAGS)

vmod_LTLIBRARIES = libvmod_accept.la

libvmod_accept_la_SOURCES = \
	vmod_accept.c

nodist_libvmod_accept_la_SOURCES = \
	vcc_accept_if.c \
	vcc_accept_if.h

dist_man_MANS = vmod_accept.3

@BUILD_VMOD_ACCEPT@

AM_TESTS_ENVIRONMENT = \
	PATH="$(VMOD_TEST_PATH)" \
	LD_LIBRARY_PATH="$(VARNISH_LIBRARY_PATH)"
TEST_EXTENSIONS = .vtc
VTC_LOG_COMPILER = varnishtest -v
AM_VTC_LOG_FLAGS = -Dvmod_accept="$(VMOD_ACCEPT)"

TESTS = @VMOD_TESTS@

EXTRA_DIST = \
	vmod_accept.vcc \
	$(VMOD_TESTS)
