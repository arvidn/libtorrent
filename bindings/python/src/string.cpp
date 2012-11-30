// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <boost/python.hpp>
#include "libtorrent/config.hpp"
#include "libtorrent/utf8.hpp"
#include <string>

#if TORRENT_USE_WSTRING

using namespace boost::python;
using namespace libtorrent;

struct unicode_from_python
{
    unicode_from_python()
    {
        converter::registry::push_back(
            &convertible, &construct, type_id<std::string>()
        );
    }

    static void* convertible(PyObject* x)
    {
#if PY_VERSION_HEX >= 0x03020000
        return PyBytes_Check(x) ? x : PyUnicode_Check(x) ? x : 0;
#else
        return PyString_Check(x) ? x : PyUnicode_Check(x) ? x : 0;
#endif
    }

    static void construct(PyObject* x, converter::rvalue_from_python_stage1_data* data)
    {
        using libtorrent::wchar_utf8;
        void* storage = ((converter::rvalue_from_python_storage<
            std::string>*)data)->storage.bytes;

        if (PyUnicode_Check(x))
        {
            std::wstring str;
            str.resize(PyUnicode_GetSize(x) + 1, 0);
#if PY_VERSION_HEX >= 0x03020000
            int len = PyUnicode_AsWideChar(x, &str[0], str.size());
#else
            int len = PyUnicode_AsWideChar((PyUnicodeObject*)x, &str[0], str.size());
#endif
            if (len > -1)
            {
               assert(len < str.size());
               str[len] = 0;
            }
            else str[str.size()-1] = 0;

            std::string utf8;
            int ret = wchar_utf8(str, utf8);
            new (storage) std::string(utf8);
        }
        else
        {
#if PY_VERSION_HEX >= 0x03000000
            new (storage) std::string(PyBytes_AsString(x));
#else
            new (storage) std::string(PyString_AsString(x));
#endif
        }
        data->convertible = storage;
    }
};

void bind_unicode_string_conversion()
{
    unicode_from_python();
}

#endif // TORRENT_USE_WSTRING

