// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include "gil.hpp"
#include <libtorrent/fingerprint.hpp>
#include "bytes.hpp"

bytes generate_fingerprint_bytes(bytes name, int major, int minor = 0, int revision = 0, int tag = 0)
{
    return lt::generate_fingerprint(name.arr, major, minor, revision, tag);
}

#if TORRENT_ABI_VERSION == 1
#include "libtorrent/aux_/disable_deprecation_warnings_push.hpp"

std::shared_ptr<lt::fingerprint> fingerprint_constructor(char const* name, int major, int minor, int revision, int tag)
{
    python_deprecated("the fingerprint class is deprecated");
    return std::make_shared<lt::fingerprint>(name, major, minor, revision, tag);
}

#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif // TORRENT_ABI_VERSION

void bind_fingerprint()
{
    using namespace boost::python;
    using namespace lt;

    def("generate_fingerprint", &generate_fingerprint);
    def("generate_fingerprint_bytes", &generate_fingerprint_bytes,
        (arg("name"), arg("major"), arg("minor") = 0, arg("revision") = 0, arg("tag") = 0));

#if TORRENT_ABI_VERSION == 1
#include "libtorrent/aux_/disable_deprecation_warnings_push.hpp"

    class_<fingerprint>("fingerprint", no_init)
        .def("__init__", make_constructor(&fingerprint_constructor))
        .def("__str__", &fingerprint::to_string)
        .def_readonly("major_version", &fingerprint::major_version)
        .def_readonly("minor_version", &fingerprint::minor_version)
        .def_readonly("revision_version", &fingerprint::revision_version)
        .def_readonly("tag_version", &fingerprint::tag_version)
        ;

#include "libtorrent/aux_/disable_warnings_pop.hpp"
#endif // TORRENT_ABI_VERSION
}
