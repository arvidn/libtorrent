// Copyright Arvid Norberg 2020. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include <libtorrent/info_hash.hpp>

namespace {

using namespace lt;

long get_hash(info_hash_t const& ih)
{
    return std::hash<info_hash_t>{}(ih);
}

}

void bind_info_hash()
{
    using namespace boost::python;
    using namespace lt;

    class_<info_hash_t>("info_hash_t")
        .def(init<sha1_hash const&>(arg("sha1_hash")))
        .def(init<sha256_hash const&>(arg("sha256_hash")))
        .def(init<sha1_hash const&, sha256_hash const&>((arg("sha1_hash"), arg("sha256_hash"))))
        .def("__hash__", get_hash)
        .def("has_v1", &info_hash_t::has_v1)
        .def("has_v2", &info_hash_t::has_v2)
        .def("has", &info_hash_t::has)
        .def("get", &info_hash_t::get)
        .def("get_best", &info_hash_t::get_best)
        .add_property("v1", &info_hash_t::v1)
        .add_property("v2", &info_hash_t::v2)
        .def(self == self)
        .def(self != self)
        .def(self < self)
        ;
}

