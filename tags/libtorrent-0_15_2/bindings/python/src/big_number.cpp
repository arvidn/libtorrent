// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <libtorrent/peer_id.hpp>
#include <boost/python.hpp>

void bind_big_number()
{
    using namespace boost::python;
    using namespace libtorrent;

    class_<big_number>("big_number")
        .def(self == self)
        .def(self != self)
        .def(self < self)
        .def(self_ns::str(self))
        .def(init<char const*>())
        ;
}

