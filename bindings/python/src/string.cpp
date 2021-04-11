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
        return PyUnicode_Check(x) ? x : nullptr;
#else
        return PyString_Check(x) ? x : PyUnicode_Check(x) ? x : nullptr;
#endif
    }

    static void construct(PyObject* x, converter::rvalue_from_python_stage1_data* data)
    {
        void* storage = ((converter::rvalue_from_python_storage<
            std::string>*)data)->storage.bytes;

#if PY_VERSION_HEX < 0x03000000
        if (PyString_Check(x))
        {
            data->convertible = new (storage) std::string(PyString_AsString(x)
                , PyString_Size(x));
        }
        else
#endif
        {
            Py_ssize_t size = 0;
            char const* unicode = PyUnicode_AsUTF8AndSize(x, &size);
            data->convertible = new (storage) std::string(unicode, size);
        }
    }
};

void bind_unicode_string_conversion()
{
    unicode_from_python();
}

