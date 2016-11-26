// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_PYTHON_HPP
#define BOOST_PYTHON_HPP

#include <libtorrent/aux_/disable_warnings_push.hpp>
#include <iostream>
#include <boost/python.hpp>
#include <libtorrent/aux_/disable_warnings_pop.hpp>

// something in here creates a define for this, presumably to make older
// versions of msvc appear to support snprintf
#ifdef snprintf
#undef snprintf
#endif

#ifdef vsnprintf
#undef vsnprintf
#endif

#endif

