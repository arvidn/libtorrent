// builds all boost.asio source as a separate compilation unit
#include <boost/version.hpp>

#ifndef BOOST_ASIO_SOURCE
#define BOOST_ASIO_SOURCE
#endif

#include "libtorrent/config.hpp"

#define TORRENT_HAS_ASIO_DECL x ## BOOST_ASIO_DECL

// only define BOOST_ASIO_DECL if it hasn't already been defined
// or if it has been defined to an empty string
#if TORRENT_HAS_ASIO_DECL == x
#ifdef BOOST_ASIO_DECL
#undef BOOST_ASIO_DECL
#endif
#define BOOST_ASIO_DECL BOOST_SYMBOL_EXPORT
#endif

#include "libtorrent/aux_/disable_warnings_push.hpp"

#if BOOST_VERSION >= 104500
#include <boost/asio/impl/src.hpp>
#elif BOOST_VERSION >= 104400
#include <boost/asio/impl/src.cpp>
#endif

#include "libtorrent/aux_/disable_warnings_pop.hpp"

