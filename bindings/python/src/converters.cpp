// Copyright Andrew Resch 2009. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include "libtorrent/socket.hpp"
#include "libtorrent/address.hpp"
#include "libtorrent/error_code.hpp"
#include "libtorrent/session_stats.hpp" // for stats_metric
#include "libtorrent/time.hpp"
#include "libtorrent/torrent_flags.hpp"
#include "libtorrent/units.hpp"
#include "libtorrent/sha1_hash.hpp"
#include "libtorrent/disk_interface.hpp" // for open_file_state
#include "libtorrent/aux_/noexcept_movable.hpp"
#include "libtorrent/peer_info.hpp"
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

template <typename Addr>
struct address_to_tuple
{
    static PyObject* convert(Addr const& addr)
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

        stl_input_iterator<T1> i(o.keys()), end;
        for (; i != end; ++i)
        {
            T1 const& key = *i;
            m[key] = extract<T2>(o[key]);
        }
        new (storage) std::map<T1, T2>(m);
        data->convertible = storage;
    }
};

template<class T>
struct vector_to_list
{
    static PyObject* convert(T const& v)
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
            &convertible, &construct, type_id<T>()
        );
    }

    static void* convertible(PyObject* x)
    {
        return PyList_Check(x) ? x: nullptr;
    }

    static void construct(PyObject* x, converter::rvalue_from_python_stage1_data* data)
    {
        void* storage = ((converter::rvalue_from_python_storage<
            T>*)data)->storage.bytes;

        T p;
        int const size = int(PyList_Size(x));
        p.reserve(size);
        for (int i = 0; i < size; ++i)
        {
           object o(borrowed(PyList_GetItem(x, i)));
           p.push_back(extract<typename T::value_type>(o));
        }
        T* ptr = new (storage) T();
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

template<class T>
struct from_bitfield_flag
{
    using underlying_type = typename T::underlying_type;

    static PyObject* convert(T const v)
    {
        object o(static_cast<underlying_type>(v));
        return incref(o.ptr());
    }
};

template<typename T>
struct to_bitfield_flag
{
   using underlying_type = typename T::underlying_type;

   to_bitfield_flag()
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
    to_python_converter<lt::address, address_to_tuple<lt::address>>();
    to_python_converter<std::pair<std::string, int>, pair_to_tuple<std::string, int>>();

    to_python_converter<std::vector<lt::stats_metric>, vector_to_list<std::vector<lt::stats_metric>>>();
    to_python_converter<std::vector<lt::open_file_state>, vector_to_list<std::vector<lt::open_file_state>>>();
    to_python_converter<std::vector<lt::sha1_hash>, vector_to_list<std::vector<lt::sha1_hash>>>();
    to_python_converter<std::vector<std::string>, vector_to_list<std::vector<std::string>>>();
    to_python_converter<std::vector<int>, vector_to_list<std::vector<int>>>();
    to_python_converter<std::vector<std::uint8_t>, vector_to_list<std::vector<std::uint8_t>>>();
    to_python_converter<std::vector<lt::tcp::endpoint>, vector_to_list<std::vector<lt::tcp::endpoint>>>();
    to_python_converter<std::vector<lt::udp::endpoint>, vector_to_list<std::vector<lt::udp::endpoint>>>();
    to_python_converter<std::vector<std::pair<std::string, int>>, vector_to_list<std::vector<std::pair<std::string, int>>>>();

    to_python_converter<lt::piece_index_t, from_strong_typedef<lt::piece_index_t>>();
    to_python_converter<lt::file_index_t, from_strong_typedef<lt::file_index_t>>();
    to_python_converter<lt::torrent_flags_t, from_bitfield_flag<lt::torrent_flags_t>>();
    to_python_converter<lt::peer_flags_t, from_bitfield_flag<lt::peer_flags_t>>();
    to_python_converter<lt::peer_source_flags_t, from_bitfield_flag<lt::peer_source_flags_t>>();
    to_python_converter<lt::bandwidth_state_flags_t, from_bitfield_flag<lt::bandwidth_state_flags_t>>();

    // work-around types
    to_python_converter<lt::aux::noexcept_movable<lt::address>, address_to_tuple<
        lt::aux::noexcept_movable<lt::address>>>();
    to_python_converter<lt::aux::noexcept_movable<lt::tcp::endpoint>, endpoint_to_tuple<
        lt::aux::noexcept_movable<lt::tcp::endpoint>>>();
    to_python_converter<lt::aux::noexcept_movable<lt::udp::endpoint>, endpoint_to_tuple<
        lt::aux::noexcept_movable<lt::udp::endpoint>>>();
    to_python_converter<lt::aux::noexcept_movable<std::vector<lt::stats_metric>>, vector_to_list<lt::aux::noexcept_movable<std::vector<lt::stats_metric>>>>();
    to_python_converter<lt::aux::noexcept_movable<std::vector<lt::open_file_state>>, vector_to_list<lt::aux::noexcept_movable<std::vector<lt::open_file_state>>>>();
    to_python_converter<lt::aux::noexcept_movable<std::vector<lt::sha1_hash>>, vector_to_list<lt::aux::noexcept_movable<std::vector<lt::sha1_hash>>>>();
    to_python_converter<lt::aux::noexcept_movable<std::vector<std::string>>, vector_to_list<lt::aux::noexcept_movable<std::vector<std::string>>>>();
    to_python_converter<lt::aux::noexcept_movable<std::vector<int>>, vector_to_list<lt::aux::noexcept_movable<std::vector<int>>>>();
    to_python_converter<lt::aux::noexcept_movable<std::vector<std::uint8_t>>, vector_to_list<lt::aux::noexcept_movable<std::vector<std::uint8_t>>>>();
    to_python_converter<lt::aux::noexcept_movable<std::vector<lt::tcp::endpoint>>, vector_to_list<lt::aux::noexcept_movable<std::vector<lt::tcp::endpoint>>>>();
    to_python_converter<lt::aux::noexcept_movable<std::vector<lt::udp::endpoint>>, vector_to_list<lt::aux::noexcept_movable<std::vector<lt::udp::endpoint>>>>();
    to_python_converter<lt::aux::noexcept_movable<std::vector<std::pair<std::string, int>>>, vector_to_list<lt::aux::noexcept_movable<std::vector<std::pair<std::string, int>>>>>();

    // python -> C++ conversions
    tuple_to_pair<int, int>();
    tuple_to_pair<std::string, int>();
    tuple_to_endpoint<lt::tcp::endpoint>();
    tuple_to_endpoint<lt::udp::endpoint>();
    tuple_to_pair<lt::piece_index_t, int>();
    dict_to_map<lt::file_index_t, std::string>();
    list_to_vector<std::vector<int>>();
    list_to_vector<std::vector<std::uint8_t>>();
    list_to_vector<std::vector<std::string>>();
    list_to_vector<std::vector<lt::tcp::endpoint>>();
    list_to_vector<std::vector<lt::udp::endpoint>>();
    list_to_vector<std::vector<std::pair<std::string, int>>>();

    // work-around types
    list_to_vector<lt::aux::noexcept_movable<std::vector<int>>>();
    list_to_vector<lt::aux::noexcept_movable<std::vector<std::uint8_t>>>();
    list_to_vector<lt::aux::noexcept_movable<std::vector<std::string>>>();
    list_to_vector<lt::aux::noexcept_movable<std::vector<lt::tcp::endpoint>>>();
    list_to_vector<lt::aux::noexcept_movable<std::vector<lt::udp::endpoint>>>();
    list_to_vector<lt::aux::noexcept_movable<std::vector<std::pair<std::string, int>>>>();

    to_strong_typedef<lt::piece_index_t>();
    to_strong_typedef<lt::file_index_t>();
    to_bitfield_flag<lt::torrent_flags_t>();
    to_bitfield_flag<lt::peer_flags_t>();
    to_bitfield_flag<lt::peer_source_flags_t>();
    to_bitfield_flag<lt::bandwidth_state_flags_t>();
}
