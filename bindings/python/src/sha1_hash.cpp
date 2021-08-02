// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include <libtorrent/sha1_hash.hpp>
#include <libtorrent/string_view.hpp>
#include <iostream>

#include "bytes.hpp"
#include "gil.hpp"

namespace {

using namespace lt;

long get_hash(sha1_hash const& s)
{
    return std::hash<sha1_hash>{}(s);
}

bytes sha1_hash_bytes(const sha1_hash& bn) {
    return bytes(bn.to_string());
}

std::shared_ptr<sha1_hash> bytes_constructor(bytes s)
{
    if (s.arr.size() < 20)
        throw std::invalid_argument("short hash length");
    if (s.arr.size() > 20)
        python_deprecated("long hash length. this will work, but is deprecated");
    return std::make_shared<sha1_hash>(s.arr);
}

std::shared_ptr<sha1_hash> string_constructor(string_view const& sv)
{
    python_deprecated("sha1_hash('str') is deprecated");
    std::string s(sv);
    if (s.size() < 20)
        throw std::invalid_argument("short hash length");
    if (s.size() > 20)
        python_deprecated("long hash length. this will work, but is deprecated");
    return std::make_shared<sha1_hash>(s);
}

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
        .def("__init__", make_constructor(&string_constructor))
        .def("__init__", make_constructor(&bytes_constructor))
        .def("clear", &sha1_hash::clear)
        .def("is_all_zeros", &sha1_hash::is_all_zeros)
        .def("to_string", sha1_hash_bytes)
        .def("__hash__", get_hash)
        .def("to_bytes", sha1_hash_bytes)
        ;

    scope().attr("peer_id") = scope().attr("sha1_hash");
}

