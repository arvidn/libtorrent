// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <libtorrent/identify_client.hpp>
#include <libtorrent/bencode.hpp>
#include <boost/python.hpp>
#include "bytes.hpp"

using namespace boost::python;
using namespace libtorrent;

struct bytes_to_python
{
    static PyObject* convert(bytes const& p)
    {
#if PY_MAJOR_VERSION >= 3
        PyObject *ret = PyBytes_FromStringAndSize(p.arr.c_str(), p.arr.size());
#else
        PyObject *ret = PyString_FromStringAndSize(p.arr.c_str(), p.arr.size());
#endif
        return ret;
    }
};

struct bytes_from_python
{
    bytes_from_python()
    {
        converter::registry::push_back(
            &convertible, &construct, type_id<bytes>());
    }

    static void* convertible(PyObject* x)
    {
#if PY_MAJOR_VERSION >= 3
        return PyBytes_Check(x) ? x : NULL;
#else
        return PyString_Check(x) ? x : NULL;
#endif
    }

    static void construct(PyObject* x, converter::rvalue_from_python_stage1_data* data)
    {
#if PY_MAJOR_VERSION >= 3
        void* storage = ((converter::rvalue_from_python_storage<bytes>*)data)->storage.bytes;
        bytes* ret = new (storage) bytes();
        ret->arr.resize(PyBytes_Size(x));
        memcpy(&ret->arr[0], PyBytes_AsString(x), ret->arr.size());
        data->convertible = storage;
#else
        void* storage = ((converter::rvalue_from_python_storage<bytes>*)data)->storage.bytes;
        bytes* ret = new (storage) bytes();
        ret->arr.resize(PyString_Size(x));
        memcpy(&ret->arr[0], PyString_AsString(x), ret->arr.size());
        data->convertible = storage;
#endif
    }
};


object client_fingerprint_(peer_id const& id)
{
    boost::optional<fingerprint> result = client_fingerprint(id);
    return result ? object(*result) : object();
}

entry bdecode_(bytes const& data)
{
    return bdecode(data.arr.begin(), data.arr.end());
}

bytes bencode_(entry const& e)
{
    bytes result;
    bencode(std::back_inserter(result.arr), e);
    return result;
}

void bind_utility()
{
    // TODO: it would be nice to install converters for sha1_hash as well
    to_python_converter<bytes, bytes_to_python>();
    bytes_from_python();

    def("identify_client", &libtorrent::identify_client);
    def("client_fingerprint", &client_fingerprint_);
    def("bdecode", &bdecode_);
    def("bencode", &bencode_);
}

