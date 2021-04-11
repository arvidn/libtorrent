// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include "gil.hpp"
#include <libtorrent/fingerprint.hpp>

void bind_fingerprint()
{
    using namespace boost::python;
    using namespace lt;

    def("generate_fingerprint", &generate_fingerprint);

#if TORRENT_ABI_VERSION == 1
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#ifdef _MSC_VER
#pragma warning(push, 1)
#pragma warning(disable: 4996)
#endif
    class_<fingerprint>("fingerprint", no_init)
        .def(
            init<char const*,int,int,int,int>(
                (arg("id"), "major", "minor", "revision", "tag")
            )
        )
        .def("__str__", depr(&fingerprint::to_string))
        .def_readonly("major_version", depr(&fingerprint::major_version))
        .def_readonly("minor_version", depr(&fingerprint::minor_version))
        .def_readonly("revision_version", depr(&fingerprint::revision_version))
        .def_readonly("tag_version", depr(&fingerprint::tag_version))
        ;
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif // TORRENT_ABI_VERSION
}
