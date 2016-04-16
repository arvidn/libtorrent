// Copyright Andrew Resch 2009. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/error_code.hpp"
#include <vector>

using namespace boost::python;
namespace bp = boost::python;
namespace lt = libtorrent;

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
        if (!PyTuple_Check(x)) return NULL;
        if (PyTuple_Size(x) != 2) return NULL;
        extract<std::string> ip(object(borrowed(PyTuple_GetItem(x, 0))));
        if (!ip.check()) return NULL;
        extract<int> port(object(borrowed(PyTuple_GetItem(x, 1))));
        if (!port.check()) return NULL;
        lt::error_code ec;
        lt::address::from_string(ip, ec);
        if (ec) return NULL;
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
        return (PyTuple_Check(x) && PyTuple_Size(x) == 2) ? x: NULL;
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
        for (int i = 0; i < v.size(); ++i)
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
            &convertible, &construct, type_id<std::vector<T> >()
        );
    }

    static void* convertible(PyObject* x)
    {
        return PyList_Check(x) ? x: 0;
    }

    static void construct(PyObject* x, converter::rvalue_from_python_stage1_data* data)
    {
        void* storage = ((converter::rvalue_from_python_storage<
            std::vector<T> >*)data)->storage.bytes;

        std::vector<T> p;
        int const size = PyList_Size(x);
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

void bind_converters()
{
    // C++ -> python conversions
    to_python_converter<std::pair<int, int>, pair_to_tuple<int, int> >();
    to_python_converter<std::pair<std::string, int>, pair_to_tuple<std::string, int> >();
    to_python_converter<lt::tcp::endpoint, endpoint_to_tuple<lt::tcp::endpoint> >();
    to_python_converter<lt::udp::endpoint, endpoint_to_tuple<lt::udp::endpoint> >();
    to_python_converter<std::vector<std::string>, vector_to_list<std::string> >();
    to_python_converter<std::vector<int>, vector_to_list<int> >();
    to_python_converter<std::vector<boost::uint8_t>, vector_to_list<boost::uint8_t> >();
    to_python_converter<std::vector<lt::tcp::endpoint>, vector_to_list<lt::tcp::endpoint> >();
    to_python_converter<std::vector<lt::udp::endpoint>, vector_to_list<lt::udp::endpoint> >();
    to_python_converter<std::vector<std::pair<std::string, int> >, vector_to_list<std::pair<std::string, int> > >();

    // python -> C++ conversions
    tuple_to_pair<int, int>();
    tuple_to_pair<std::string, int>();
    tuple_to_endpoint<lt::tcp::endpoint>();
    tuple_to_endpoint<lt::udp::endpoint>();
    list_to_vector<int>();
    list_to_vector<boost::uint8_t>();
    list_to_vector<std::string>();
    list_to_vector<lt::tcp::endpoint>();
    list_to_vector<lt::udp::endpoint>();
    list_to_vector<std::pair<std::string, int> >();
}

