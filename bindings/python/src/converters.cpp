// Copyright Andrew Resch 2009. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/session_stats.hpp" // for stats_metric
#include "libtorrent/file_pool.hpp" // for file_pool_status

using namespace boost::python;
namespace bp = boost::python;

template<class T1, class T2>
struct pair_to_tuple
{
    static PyObject* convert(const std::pair<T1, T2>& p)
    {
        return incref(bp::make_tuple(p.first, p.second).ptr());
    }
};

template <typename Endpoint>
struct endpoint_to_tuple
{
    static PyObject* convert(Endpoint const& ep)
    {
        return incref(bp::object(bp::make_tuple(ep.address().to_string(), ep.port())).ptr());
    }
};

struct address_to_tuple
{
    static PyObject* convert(libtorrent::address const& addr)
    {
        libtorrent::error_code ec;
        return incref(bp::object(addr.to_string(ec)).ptr());
    }
};

template<class T1, class T2>
struct tuple_to_pair
{
    tuple_to_pair()
    {
        converter::registry::push_back(
            &convertible, &construct, type_id<std::pair<T1, T2> >()
        );
    }

    static void* convertible(PyObject* x)
    {
        return PyTuple_Check(x) ? x: 0;
    }

    static void construct(PyObject* x, converter::rvalue_from_python_stage1_data* data)
    {
        void* storage = ((converter::rvalue_from_python_storage<
            std::pair<T1, T2> >*)data)->storage.bytes;

        object o(borrowed(x));
        std::pair<T1, T2> p;
        p.first = extract<T1>(o[0]);
        p.second = extract<T2>(o[1]);
        new (storage) std::pair<T1, T2>(p);
        data->convertible = storage;
    }
};

template<class T>
struct vector_to_list
{
    static PyObject* convert(const std::vector<T>& v)
    {
        list l;
        for (int i = 0; i < int(v.size()); ++i)
        {
           l.append(v[i]);
        }
        return incref(l.ptr());
    }
};

void bind_converters()
{
    namespace lt = libtorrent;
    to_python_converter<std::pair<int, int>, pair_to_tuple<int, int> >();
    to_python_converter<lt::tcp::endpoint, endpoint_to_tuple<lt::tcp::endpoint> >();
    to_python_converter<lt::udp::endpoint, endpoint_to_tuple<lt::udp::endpoint> >();
    to_python_converter<lt::address, address_to_tuple>();
    tuple_to_pair<int, int>();

    to_python_converter<std::vector<lt::stats_metric>, vector_to_list<lt::stats_metric> >();
    to_python_converter<std::vector<lt::pool_file_status>, vector_to_list<lt::pool_file_status> >();
    to_python_converter<std::vector<std::string>, vector_to_list<std::string> >();
    to_python_converter<std::vector<lt::sha1_hash>, vector_to_list<lt::sha1_hash> >();
}
