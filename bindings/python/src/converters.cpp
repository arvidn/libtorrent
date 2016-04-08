// Copyright Andrew Resch 2009. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"

using namespace boost::python;

template<class T1, class T2>
struct pair_to_tuple
{
    static PyObject* convert(const std::pair<T1, T2>& p)
    {
        return incref(make_tuple(p.first, p.second).ptr());
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

void bind_converters()
{
    to_python_converter<std::pair<int, int>, pair_to_tuple<int, int> >();
    tuple_to_pair<int, int>();
}
