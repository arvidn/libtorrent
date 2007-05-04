// Copyright Daniel Wallin 2007. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef OPTIONAL_070108_HPP
# define OPTIONAL_070108_HPP

# include <boost/python.hpp>
# include <boost/optional.hpp>

template <class T>
struct optional_to_python
{
    optional_to_python()
    {
        boost::python::to_python_converter<
            boost::optional<T>, optional_to_python<T>
        >();
    }

    static PyObject* convert(boost::optional<T> const& x)
    {
        if (!x)
            return boost::python::incref(Py_None);

        return boost::python::incref(boost::python::object(*x).ptr());
    }
};

#endif // OPTIONAL_070108_HPP

