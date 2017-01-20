AM_CFLAGS = $(VARNISHAPI_CFLAGS)
	-Wall \
	-Werror \
	-Werror \
	-Wall \
	-Wno-format-y2k \
	-W \
	-Wstrict-prototypes \
	-Wmissing-prototypes \
	-Werror=missing-field-initializers \
	-Wpointer-arith \
	-Wreturn-type \
	-Wwrite-strings \
	-Wcast-qual \
	-Wswitch \
	-Wshadow \
	-Wunused-parameter \
	-Wcast-align \
	-Wchar-subscripts \
	-Wnested-externs \
	-Wextra \
	-Wno-sign-compare

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
