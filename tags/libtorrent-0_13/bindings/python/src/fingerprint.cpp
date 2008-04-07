// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <libtorrent/fingerprint.hpp>
#include <boost/python.hpp>

void bind_fingerprint()
{
    using namespace boost::python;
    using namespace libtorrent;

    class_<fingerprint>("fingerprint", no_init)
        .def(
            init<char const*,int,int,int,int>(
                (arg("id"), "major", "minor", "revision", "tag")
            )
        )
        .def("__str__", &fingerprint::to_string)
        .def_readonly("name", &fingerprint::name)
        .def_readonly("major_version", &fingerprint::major_version)
        .def_readonly("minor_version", &fingerprint::minor_version)
        .def_readonly("revision_version", &fingerprint::revision_version)
        .def_readonly("tag_version", &fingerprint::tag_version)
        ;
}

