/*

Copyright (c) 2011, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include <boost/python.hpp>
#include <libtorrent/error_code.hpp>

using namespace boost::python;
using namespace libtorrent;
using boost::system::error_category;

void bind_error_code()
{
    class_<boost::system::error_category, boost::noncopyable>("error_category", no_init)
        .def("name", &error_category::name)
        .def("message", &error_category::message)
        .def(self == self)
        .def(self < self)
        .def(self != self)
        ;

    class_<error_code>("error_code")
        .def(init<>())
        .def("message", &error_code::message)
        .def("value", &error_code::value)
        .def("clear", &error_code::clear)
        .def("category", &error_code::category
           , return_internal_reference<>())
        .def("assign", &error_code::assign)
        ;

    def("get_libtorrent_category", &get_libtorrent_category
       , return_internal_reference<>());

    def("generic_category", &boost::system::generic_category
       , return_internal_reference<>());

    def("system_category", &boost::system::system_category
       , return_internal_reference<>());
}

