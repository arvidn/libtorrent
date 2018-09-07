// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include <libtorrent/sha1_hash.hpp>
#include <iostream>

#include "bytes.hpp"

long get_hash(boost::python::object o)
{
    using namespace boost::python;
    return long(PyObject_Hash(str(o).ptr()));
}

using namespace lt;

bytes sha1_hash_bytes(const sha1_hash& bn) {
    return bytes(bn.to_string());
}

void bind_sha1_hash()
{
    using namespace boost::python;
    using namespace lt;

    class_<sha1_hash>("sha1_hash")
        .def(self == self)
        .def(self != self)
        .def(self < self)
        .def(self_ns::str(self))
        .def(init<std::string>())
        .def("clear", &sha1_hash::clear)
        .def("is_all_zeros", &sha1_hash::is_all_zeros)
        .def("to_string", &sha1_hash::to_string)
        .def("__hash__", get_hash)
        .def("to_bytes", sha1_hash_bytes)
        ;

    scope().attr("peer_id") = scope().attr("sha1_hash");
}

