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

#include "boost_python.hpp"
#include <libtorrent/error_code.hpp>
#include <libtorrent/bdecode.hpp>
#include <libtorrent/upnp.hpp>
#include <libtorrent/socks5_stream.hpp>

namespace boost
{
	// this fixe mysterious link error on msvc
	template <>
	inline boost::system::error_category const volatile*
	get_pointer(class boost::system::error_category const volatile* p)
	{
		return p;
	}
}

#include <boost/asio/error.hpp>
#if TORRENT_USE_SSL
#include <libtorrent/ssl.hpp>
#endif
#if TORRENT_USE_I2P
#include <libtorrent/i2p_stream.hpp>
#endif

using namespace boost::python;
using namespace lt;
using boost::system::error_category;

namespace {

	struct ec_pickle_suite : boost::python::pickle_suite
	{
		static boost::python::tuple
		getinitargs(error_code const&)
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
				ec.assign(value, lt::system_category());
			else if (category == "generic")
				ec.assign(value, lt::generic_category());
			else if (category == "libtorrent")
				ec.assign(value, lt::libtorrent_category());
			else if (category == "http error")
				ec.assign(value, lt::http_category());
			else if (category == "UPnP error")
				ec.assign(value, lt::upnp_category());
			else if (category == "bdecode error")
				ec.assign(value, lt::bdecode_category());
			else if (category == "asio.netdb")
				ec.assign(value, boost::asio::error::get_netdb_category());
			else if (category == "asio.addinfo")
				ec.assign(value, boost::asio::error::get_addrinfo_category());
			else if (category == "asio.misc")
				ec.assign(value, boost::asio::error::get_misc_category());
#if TORRENT_USE_SSL
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

struct category_holder
{
	category_holder(boost::system::error_category const& cat) : m_cat(&cat) {}
	char const* name() const { return m_cat->name(); }
	std::string message(int const v) const { return m_cat->message(v); }

	friend bool operator==(category_holder const lhs, category_holder const rhs)
	{ return *lhs.m_cat == *rhs.m_cat; }

	friend bool operator!=(category_holder const lhs, category_holder const rhs)
	{ return *lhs.m_cat != *rhs.m_cat; }

	friend bool operator<(category_holder const lhs, category_holder const rhs)
	{ return *lhs.m_cat < *rhs.m_cat; }

	boost::system::error_category const& ref() const { return *m_cat; }
	operator boost::system::error_category const&() const { return *m_cat; }
private:
	boost::system::error_category const* m_cat;
};

void error_code_assign(boost::system::error_code& me, int const v, category_holder const cat)
{
	me.assign(v, cat.ref());
}

category_holder error_code_category(boost::system::error_code const& me)
{
	return category_holder(me.category());
}

#define WRAP_CAT(name) \
	category_holder wrap_ ##name## _category() { return category_holder(name## _category()); }

WRAP_CAT(libtorrent)
WRAP_CAT(upnp)
WRAP_CAT(http)
WRAP_CAT(socks)
WRAP_CAT(bdecode)
#if TORRENT_USE_I2P
WRAP_CAT(i2p)
#endif
WRAP_CAT(generic)
WRAP_CAT(system)

#undef WRAP_CAT

void bind_error_code()
{
    class_<category_holder>("error_category", no_init)
        .def("name", &category_holder::name)
        .def("message", &category_holder::message)
        .def(self == self)
        .def(self < self)
        .def(self != self)
        ;

    class_<error_code>("error_code")
        .def(init<>())
        .def(init<int, category_holder>())
        .def("message", static_cast<std::string (error_code::*)() const>(&error_code::message))
        .def("value", &error_code::value)
        .def("clear", &error_code::clear)
        .def("category", &error_code_category)
        .def("assign", &error_code_assign)
        .def_pickle(ec_pickle_suite())
        ;

    def("libtorrent_category", &wrap_libtorrent_category);
    def("upnp_category", &wrap_upnp_category);
    def("http_category", &wrap_http_category);
    def("socks_category", &wrap_socks_category);
    def("bdecode_category", &wrap_bdecode_category);
#if TORRENT_USE_I2P
    def("i2p_category", &wrap_i2p_category);
#endif

#if TORRENT_ABI_VERSION == 1
    def("get_libtorrent_category", &wrap_libtorrent_category);
    def("get_upnp_category", &wrap_upnp_category);
    def("get_http_category", &wrap_http_category);
    def("get_socks_category", &wrap_socks_category);
    def("get_bdecode_category", &wrap_bdecode_category);
#if TORRENT_USE_I2P
    def("get_i2p_category", &wrap_i2p_category);
#endif
#endif // TORRENT_ABI_VERSION

    def("generic_category", &wrap_generic_category);

    def("system_category", &wrap_system_category);
}

