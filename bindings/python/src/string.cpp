// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include <string>

using namespace boost::python;

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
        void* storage = ((converter::rvalue_from_python_storage<
            std::string>*)data)->storage.bytes;

        if (PyUnicode_Check(x))
        {
            PyObject* utf8 = PyUnicode_AsUTF8String(x);
            if (utf8 == NULL)
            {
               new (storage) std::string();
            }
            else
            {
#if PY_VERSION_HEX >= 0x03000000
               new (storage) std::string(PyBytes_AsString(utf8)
                  , PyBytes_Size(utf8));
#else
               new (storage) std::string(PyString_AsString(utf8)
                     , PyString_Size(utf8));
#endif
               Py_DECREF(utf8);
            }
        }
        else
        {
#if PY_VERSION_HEX >= 0x03000000
            new (storage) std::string(PyBytes_AsString(x), PyBytes_Size(x));
#else
            new (storage) std::string(PyString_AsString(x)
               , PyString_Size(x));
#endif
        }
        data->convertible = storage;
    }
};

void bind_unicode_string_conversion()
{
    unicode_from_python();
}

