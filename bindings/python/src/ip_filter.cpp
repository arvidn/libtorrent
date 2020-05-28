// Copyright Andrew Resch 2008. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include <libtorrent/ip_filter.hpp>
#include "gil.hpp"

using namespace boost::python;
using namespace lt;

namespace
{
    void add_rule(ip_filter& filter, std::string start, std::string end, int flags)
    {
        return filter.add_rule(make_address(start), make_address(end), flags);
    }

    int access0(ip_filter& filter, std::string addr)
    {
        return filter.access(make_address(addr));
    }
}

void bind_ip_filter()
{
    class_<ip_filter>("ip_filter")
        .def("add_rule", add_rule)
        .def("access", access0)
        .def("export_filter", allow_threads(&ip_filter::export_filter))
    ;
}
