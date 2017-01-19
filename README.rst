============
vmod-accept
============

SYNOPSIS
========

import accept;

DESCRIPTION
===========

accept allows you to sanitize the Accept* headers (mainly Accept,
Accept-Charset and Accept-Encoding) by specify one fallback string, and then
adding valid values::

        new rule = accept.rule("text/plain");
        rule.add("text/html");
        rule.add("application/json");

You can then use the ``rule`` object to filter the headers. The following line
will set the accept header to "text/html" or "application/json" if any of them
is found in the original header, and will set it to "text/plain" if neither is
found::

        set req.http.Accept = rule.filter(req.http.Accept);

accept will ignore any parameter found and will just return the first choice
found. More info here: https://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html

If the idea of this vmod rings a bell, it's probably because it started as a
replacement for @cosimo's `varnish-accept-language VCL module
<https://github.com/cosimo/varnish-accept-language`_, before the author noticed
`his new version<https://github.com/cosimo/libvmod-i18n>`_. However, vmod-accept
tries to be more generic, and should be usable with all accept headers and not
just the accept-language ones.

FUNCTIONS
=========

The full API is listed in src/vmod_str.vcc.

INSTALLATION
============

The source tree is based on autotools to configure the building, and
does also have the necessary bits in place to do functional unit tests
using the ``varnishtest`` tool.

Building requires the Varnish header files and uses pkg-config to find
the necessary paths.

Usage::

 ./autogen.sh
 ./configure

If you have installed Varnish to a non-standard directory, call
``autogen.sh`` and ``configure`` with ``PKG_CONFIG_PATH`` pointing to
the appropriate path. For instance, when varnishd configure was called
with ``--prefix=$PREFIX``, use

::

 export PKG_CONFIG_PATH=${PREFIX}/lib/pkgconfig
 export ACLOCAL_PATH=${PREFIX}/share/aclocal

The module will inherit its prefix from Varnish, unless you specify a
different ``--prefix`` when running the ``configure`` script for this
module.

Make targets:

* make - builds the vmod.
* make install - installs your vmod.
* make check - runs the unit tests in ``src/tests/*.vtc``.
* make distcheck - run check and prepare a tarball of the vmod.

If you build a dist tarball, you don't need any of the autotools or
pkg-config. You can build the module simply by running::

 ./configure
 make

Installation directories
------------------------

By default, the vmod ``configure`` script installs the built vmod in the
directory relevant to the prefix. The vmod installation directory can be
overridden by passing the ``vmoddir`` variable to ``make install``.

USAGE
=====

In your VCL you could then use this vmod along the following lines::

        import accept;

        sub vcl_init {
                new rule = accept.rule("en");
                rule.add("en");
                rule.add("de");
        }

        sub vcl_recv {
                # filter the Accept-Language header, returning "en" or "de" if
                # found in the header, and "en" otherwise.
                set req.http.Accept-Language = rule.filter(req.http.Accept-Language);
        }

COMMON PROBLEMS
===============

* configure: error: Need varnish.m4 -- see README.rst

  Check whether ``PKG_CONFIG_PATH`` and ``ACLOCAL_PATH`` were set correctly
  before calling ``autogen.sh`` and ``configure``

* Incompatibilities with different Varnish Cache versions

  Make sure you build this vmod against its correspondent Varnish Cache version.
  For instance, to build against Varnish Cache 4.1, this vmod must be built from
  branch 4.1.
