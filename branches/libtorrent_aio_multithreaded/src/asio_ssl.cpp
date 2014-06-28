// builds all boost.asio SSL source as a separate compilation unit
#include <boost/version.hpp>
#include <climits>

#if BOOST_VERSION >= 104610
#include <boost/asio/ssl/impl/src.hpp>
#endif

