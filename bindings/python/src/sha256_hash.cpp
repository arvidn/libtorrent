// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include <libtorrent/sha1_hash.hpp>
#include <iostream>

#include "bytes.hpp"

namespace {

using namespace lt;

long get_hash(sha256_hash const& s)
{
    return std::hash<sha256_hash>{}(s);
}

bytes sha256_hash_bytes(const sha256_hash& bn) {
    return bytes(bn.to_string());
}

}

void bind_sha256_hash()
{
    using namespace boost::python;
    using namespace lt;

    class_<sha256_hash>("sha256_hash")
        .def(self == self)
        .def(self != self)
        .def(self < self)
        .def(self_ns::str(self))
        .def(init<std::string>())
        .def("clear", &sha256_hash::clear)
        .def("is_all_zeros", &sha256_hash::is_all_zeros)
        .def("to_string", sha256_hash_bytes)
        .def("__hash__", get_hash)
        .def("to_bytes", sha256_hash_bytes)
        ;
}

