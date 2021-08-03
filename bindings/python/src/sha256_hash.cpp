// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include <libtorrent/sha1_hash.hpp>
#include <iostream>

#include "bytes.hpp"
#include "gil.hpp"
#include <libtorrent/string_view.hpp>

namespace {

using namespace lt;

long get_hash(sha256_hash const& s)
{
    return std::hash<sha256_hash>{}(s);
}

bytes sha256_hash_bytes(const sha256_hash& bn) {
    return bytes(bn.to_string());
}

std::shared_ptr<sha256_hash> bytes_constructor(bytes s)
{
    if (s.arr.size() < 32)
        throw std::invalid_argument("short hash length");
    if (s.arr.size() > 32)
        python_deprecated("long hash length. this will work, but is deprecated");
    return std::make_shared<sha256_hash>(s.arr);
}

std::shared_ptr<sha256_hash> string_constructor(string_view const sv)
{
    python_deprecated("sha256_hash('str') is deprecated");
    std::string s(sv);
    if (s.size() < 32)
        throw std::invalid_argument("short hash length");
    if (s.size() > 32)
        python_deprecated("long hash length. this will work, but is deprecated");
    return std::make_shared<sha256_hash>(s);
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
        .def("__init__", make_constructor(&string_constructor))
        .def("__init__", make_constructor(&bytes_constructor))
        .def("clear", &sha256_hash::clear)
        .def("is_all_zeros", &sha256_hash::is_all_zeros)
        .def("to_string", sha256_hash_bytes)
        .def("__hash__", get_hash)
        .def("to_bytes", sha256_hash_bytes)
        ;
}

