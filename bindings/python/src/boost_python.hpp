// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_PYTHON_HPP
#define BOOST_PYTHON_HPP

#include <cstdio>
#include <libtorrent/aux_/disable_warnings_push.hpp>
#include <boost/python.hpp>

#include <boost/bind/placeholders.hpp>

// in boost 1.60, placeholders moved into a namespace, just like std
#if BOOST_VERSION >= 106000
using namespace boost::placeholders;
#endif

#include <boost/python/stl_iterator.hpp>
#include <boost/get_pointer.hpp>

#include <libtorrent/aux_/disable_warnings_pop.hpp>

#include <iostream>

// something in here creates a define for this, presumably to make older
// versions of msvc appear to support snprintf
#ifdef snprintf
#undef snprintf
#endif

#ifdef vsnprintf
#undef vsnprintf
#endif

#endif

