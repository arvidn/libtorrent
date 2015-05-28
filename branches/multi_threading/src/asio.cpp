// builds all boost.asio source as a separate compilation unit
#include <boost/version.hpp>

#include "libtorrent/config.hpp"

// only define BOOST_ASIO_DECL if it hasn't already been defined
// or if it has been defined to an empty string
#define TORRENT_IS_EMPTY_IMPL(VAL)  VAL ## 1
#define TORRENT_IS_EMPTY(VAL) TORRENT_IS_EMPTY_IMPL(VAL)

#if !defined BOOST_ASIO_DECL || TORRENT_IS_EMPTY(BOOST_ASIO_DECL) == 1
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

