// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <libtorrent/peer_id.hpp>
#include <boost/python.hpp>
#include "bytes.hpp"

long get_hash(boost::python::object o)
{
    using namespace boost::python;
    return PyObject_Hash(str(o).ptr());
}

using namespace libtorrent;

bytes big_number_bytes(const big_number& bn) {
    return bytes(bn.to_string());
}

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
        .def("clear", &big_number::clear)
        .def("is_all_zeros", &big_number::is_all_zeros)
        .def("to_bytes", big_number_bytes)
        .def("__hash__", get_hash)
        ;
}

