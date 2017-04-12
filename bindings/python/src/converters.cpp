// Copyright Andrew Resch 2009. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/session_stats.hpp" // for stats_metric
#include "libtorrent/time.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/disk_interface.hpp" // for open_file_state
#include <vector>

using namespace boost::python;
namespace bp = boost::python;

template<class T>
struct endpoint_to_tuple
{
    static PyObject* convert(T const& ep)
    {
        return incref(bp::make_tuple(ep.address().to_string(), ep.port()).ptr());
    }
};

template<class T>
struct tuple_to_endpoint
{
    tuple_to_endpoint()
    {
        converter::registry::push_back(
            &convertible, &construct, type_id<T>()
        );
    }

    static void* convertible(PyObject* x)
    {
        if (!PyTuple_Check(x)) return nullptr;
        if (PyTuple_Size(x) != 2) return nullptr;
        extract<std::string> ip(object(borrowed(PyTuple_GetItem(x, 0))));
        if (!ip.check()) return nullptr;
        extract<int> port(object(borrowed(PyTuple_GetItem(x, 1))));
        if (!port.check()) return nullptr;
        lt::error_code ec;
        lt::address::from_string(ip, ec);
        if (ec) return nullptr;
        return x;
    }

    static void construct(PyObject* x, converter::rvalue_from_python_stage1_data* data)
    {
        void* storage = ((converter::rvalue_from_python_storage<T*>*)data)
           ->storage.bytes;

        object o(borrowed(x));
        new (storage) T(lt::address::from_string(
           extract<std::string>(o[0])), extract<int>(o[1]));
        data->convertible = storage;
    }
};

template<class T1, class T2>
struct pair_to_tuple
{
    static PyObject* convert(const std::pair<T1, T2>& p)
    {
        return incref(bp::make_tuple(p.first, p.second).ptr());
    }
};

struct address_to_tuple
{
    static PyObject* convert(lt::address const& addr)
    {
        lt::error_code ec;
        return incref(bp::object(addr.to_string(ec)).ptr());
    }
};

template<class T1, class T2>
struct tuple_to_pair
{
    tuple_to_pair()
    {
        converter::registry::push_back(
            &convertible, &construct, type_id<std::pair<T1, T2>>()
        );
    }

    static void* convertible(PyObject* x)
    {
        return (PyTuple_Check(x) && PyTuple_Size(x) == 2) ? x: nullptr;
    }

    static void construct(PyObject* x, converter::rvalue_from_python_stage1_data* data)
    {
        void* storage = ((converter::rvalue_from_python_storage<
            std::pair<T1, T2>>*)data)->storage.bytes;

        object o(borrowed(x));
        std::pair<T1, T2> p;
        p.first = extract<T1>(o[0]);
        p.second = extract<T2>(o[1]);
        new (storage) std::pair<T1, T2>(p);
        data->convertible = storage;
    }
};

template<class T1, class T2>
struct dict_to_map
{
    dict_to_map()
    {
        converter::registry::push_back(
            &convertible, &construct, type_id<std::map<T1, T2>>()
        );
    }

    static void* convertible(PyObject* x)
    {
        return PyDict_Check(x) ? x: nullptr;
    }

    static void construct(PyObject* x, converter::rvalue_from_python_stage1_data* data)
    {
        void* storage = ((converter::rvalue_from_python_storage<
            std::map<T1, T2>>*)data)->storage.bytes;

        dict o(borrowed(x));
        std::map<T1, T2> m;

        list iterkeys = (list)o.keys();
        int const len = int(boost::python::len(iterkeys));
        for (int i = 0; i < len; i++)
        {
            object key = iterkeys[i];
            m[extract<T1>(key)] = extract<T2>(o[key]);
        }
        new (storage) std::map<T1, T2>(m);
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

template<class T>
struct list_to_vector
{
    list_to_vector()
    {
        converter::registry::push_back(
            &convertible, &construct, type_id<std::vector<T>>()
        );
    }

    static void* convertible(PyObject* x)
    {
        return PyList_Check(x) ? x: nullptr;
    }

    static void construct(PyObject* x, converter::rvalue_from_python_stage1_data* data)
    {
        void* storage = ((converter::rvalue_from_python_storage<
            std::vector<T>>*)data)->storage.bytes;

        std::vector<T> p;
        int const size = int(PyList_Size(x));
        p.reserve(size);
        for (int i = 0; i < size; ++i)
        {
           object o(borrowed(PyList_GetItem(x, i)));
           p.push_back(extract<T>(o));
        }
        std::vector<T>* ptr = new (storage) std::vector<T>();
        ptr->swap(p);
        data->convertible = storage;
    }
};

template<class T>
struct from_strong_typedef
{
    using underlying_type = typename T::underlying_type;

    static PyObject* convert(const T& v)
    {
        object o(static_cast<underlying_type>(v));
        return incref(o.ptr());
    }
};

template<typename T>
struct to_strong_typedef
{
   using underlying_type = typename T::underlying_type;

   to_strong_typedef()
   {
        converter::registry::push_back(
            &convertible, &construct, type_id<T>()
        );
    }

    static void* convertible(PyObject* x)
    {
        return PyNumber_Check(x) ? x : nullptr;
    }

    static void construct(PyObject* x, converter::rvalue_from_python_stage1_data* data)
    {
        void* storage = ((converter::rvalue_from_python_storage<T>*)data)->storage.bytes;
        new (storage) T(extract<underlying_type>(object(borrowed(x))));
        data->convertible = storage;
    }
};

void bind_converters()
{
    // C++ -> python conversions
    to_python_converter<std::pair<int, int>, pair_to_tuple<int, int>>();
    to_python_converter<std::pair<lt::piece_index_t, int>, pair_to_tuple<lt::piece_index_t, int>>();
    to_python_converter<lt::tcp::endpoint, endpoint_to_tuple<lt::tcp::endpoint>>();
    to_python_converter<lt::udp::endpoint, endpoint_to_tuple<lt::udp::endpoint>>();
    to_python_converter<lt::address, address_to_tuple>();
    to_python_converter<std::pair<std::string, int>, pair_to_tuple<std::string, int>>();

    to_python_converter<std::vector<lt::stats_metric>, vector_to_list<lt::stats_metric>>();
    to_python_converter<std::vector<lt::open_file_state>, vector_to_list<lt::open_file_state>>();
    to_python_converter<std::vector<lt::sha1_hash>, vector_to_list<lt::sha1_hash>>();
    to_python_converter<std::vector<std::string>, vector_to_list<std::string>>();
    to_python_converter<std::vector<int>, vector_to_list<int>>();
    to_python_converter<std::vector<std::uint8_t>, vector_to_list<std::uint8_t>>();
    to_python_converter<std::vector<lt::tcp::endpoint>, vector_to_list<lt::tcp::endpoint>>();
    to_python_converter<std::vector<lt::udp::endpoint>, vector_to_list<lt::udp::endpoint>>();
    to_python_converter<std::vector<std::pair<std::string, int>>, vector_to_list<std::pair<std::string, int>>>();

    to_python_converter<lt::piece_index_t, from_strong_typedef<lt::piece_index_t>>();
    to_python_converter<lt::file_index_t, from_strong_typedef<lt::file_index_t>>();

    // python -> C++ conversions
    tuple_to_pair<int, int>();
    tuple_to_pair<std::string, int>();
    tuple_to_endpoint<lt::tcp::endpoint>();
    tuple_to_endpoint<lt::udp::endpoint>();
    tuple_to_pair<lt::piece_index_t, int>();
    dict_to_map<lt::file_index_t, std::string>();
    list_to_vector<int>();
    list_to_vector<std::uint8_t>();
    list_to_vector<std::string>();
    list_to_vector<lt::tcp::endpoint>();
    list_to_vector<lt::udp::endpoint>();
    list_to_vector<std::pair<std::string, int>>();

    to_strong_typedef<lt::piece_index_t>();
    to_strong_typedef<lt::file_index_t>();
}
