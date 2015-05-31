// builds all boost.asio source as a separate compilation unit
#include <boost/version.hpp>
#include <boost/preprocessor/facilities/is_empty.hpp>

#include "libtorrent/config.hpp"

// only define BOOST_ASIO_DECL if it hasn't already been defined
// or if it has been defined to an empty string

#if !defined BOOST_ASIO_DECL || !BOOST_PP_IS_EMPTY(BOOST_ASIO_DECL)
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

