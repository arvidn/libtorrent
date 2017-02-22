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

#include <libtorrent/error_code.hpp>
#include <libtorrent/bdecode.hpp>
#include <libtorrent/upnp.hpp>
#include <libtorrent/socks5_stream.hpp>
#include <boost/asio/error.hpp>
#if defined TORRENT_USE_OPENSSL
#include <boost/asio/ssl/error.hpp>
#endif
#include "boost_python.hpp"

using namespace boost::python;
using namespace libtorrent;
using boost::system::error_category;

namespace {

	struct ec_pickle_suite : boost::python::pickle_suite
	{
		static boost::python::tuple
		getinitargs(error_code const& ec)
		{
			return boost::python::tuple();
		}

		static boost::python::tuple
		getstate(error_code const& ec)
		{
			return boost::python::make_tuple(ec.value(), ec.category().name());
		}

		static void
		setstate(error_code& ec, boost::python::tuple state)
		{
			using namespace boost::python;
			if (len(state) != 2)
			{
				PyErr_SetObject(PyExc_ValueError,
					("expected 2-item tuple in call to __setstate__; got %s"
					% state).ptr());
				throw_error_already_set();
			}

			int const value = extract<int>(state[0]);
			std::string const category = extract<std::string>(state[1]);
			if (category == "system")
				ec.assign(value, libtorrent::system_category());
			else if (category == "generic")
				ec.assign(value, libtorrent::generic_category());
			else if (category == "libtorrent")
				ec.assign(value, libtorrent::libtorrent_category());
			else if (category == "http error")
				ec.assign(value, libtorrent::http_category());
			else if (category == "UPnP error")
				ec.assign(value, libtorrent::upnp_category());
			else if (category == "bdecode error")
				ec.assign(value, libtorrent::bdecode_category());
			else if (category == "asio.netdb")
				ec.assign(value, boost::asio::error::get_netdb_category());
			else if (category == "asio.addinfo")
				ec.assign(value, boost::asio::error::get_addrinfo_category());
			else if (category == "asio.misc")
				ec.assign(value, boost::asio::error::get_misc_category());
			else if (category == "asio.misc")
				ec.assign(value, boost::asio::error::get_misc_category());
#if defined TORRENT_USE_OPENSSL
			else if (category == "asio.ssl")
				ec.assign(value, boost::asio::error::get_ssl_category());
#endif
			else
			{
				PyErr_SetObject(PyExc_ValueError,
					("unexpected error_category passed to __setstate__; got '%s'"
					% object(category)).ptr());
				throw_error_already_set();
			}
		}
	};
}

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
        .def_pickle(ec_pickle_suite())
        ;

typedef return_value_policy<reference_existing_object> return_existing;

    def("libtorrent_category", &libtorrent_category, return_existing());
    def("upnp_category", &upnp_category, return_existing());
    def("http_category", &http_category, return_existing());
    def("socks_category", &socks_category, return_existing());
    def("bdecode_category", &bdecode_category, return_existing());
#if TORRENT_USE_I2P
    def("i2p_category", &i2p_category, return_existing());
#endif

#ifndef TORRENT_NO_DEPRECATE
    def("get_libtorrent_category", &libtorrent_category, return_existing());
    def("get_upnp_category", &upnp_category, return_existing());
    def("get_http_category", &http_category, return_existing());
    def("get_socks_category", &socks_category, return_existing());
    def("get_bdecode_category", &bdecode_category, return_existing());
#if TORRENT_USE_I2P
    def("get_i2p_category", &i2p_category, return_existing());
#endif
#endif // TORRENT_NO_DEPRECATE

    def("generic_category", &boost::system::generic_category, return_existing());

    def("system_category", &boost::system::system_category, return_existing());
}

