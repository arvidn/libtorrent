// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <libtorrent/session.hpp>
#include <boost/python.hpp>

using namespace boost::python;
using namespace libtorrent;

struct entry_to_python
{
    static object convert(entry::list_type const& l)
    {
        list result;

        for (entry::list_type::const_iterator i(l.begin()), e(l.end()); i != e; ++i)
        {
            result.append(*i);
        }

        return result;
    }

    static object convert(entry::dictionary_type const& d)
    {
        dict result;

        for (entry::dictionary_type::const_iterator i(d.begin()), e(d.end()); i != e; ++i)
            result[i->first] = i->second;

        return result;
    }

    static object convert0(entry const& e)
    {
        switch (e.type())
        {
        case entry::int_t:
            return object(e.integer());
        case entry::string_t:
            return object(e.string());
        case entry::list_t:
            return convert(e.list());
        case entry::dictionary_t:
            return convert(e.dict());
        default:
            return object();
        }
    }

    static PyObject* convert(boost::shared_ptr<entry> const& e)
    {
        if (!e)
            return incref(Py_None);
        return convert(*e);
    }

    static PyObject* convert(entry const& e)
    {
        return incref(convert0(e).ptr());
    }
};

struct entry_from_python
{
    entry_from_python()
    {
        converter::registry::push_back(
            &convertible, &construct, type_id<entry>()
        );
    }

    static void* convertible(PyObject* e)
    {
        return e;
    }

    static entry construct0(object e)
    {
        if (extract<dict>(e).check())
        {
            dict d = extract<dict>(e);
            list items(d.items());
            std::size_t length = extract<std::size_t>(items.attr("__len__")());
            entry result(entry::dictionary_t);

            for (std::size_t i = 0; i < length; ++i)
            {
                result.dict().insert(
                    std::make_pair(
                        extract<char const*>(items[i][0])()
                      , construct0(items[i][1])
                    )
                );
            }

            return result;
        }
        else if (extract<list>(e).check())
        {
            list l = extract<list>(e);

            std::size_t length = extract<std::size_t>(l.attr("__len__")());
            entry result(entry::list_t);

            for (std::size_t i = 0; i < length; ++i)
            {
                result.list().push_back(construct0(l[i]));
            }

            return result;
        }
        else if (extract<str>(e).check())
        {
            return entry(extract<std::string>(e)());
        }
        else if (extract<entry::integer_type>(e).check())
        {
            return entry(extract<entry::integer_type>(e)());
        }

        return entry();
    }

    static void construct(PyObject* e, converter::rvalue_from_python_stage1_data* data)
    {
        void* storage = ((converter::rvalue_from_python_storage<entry>*)data)->storage.bytes;
        new (storage) entry(construct0(object(borrowed(e))));
        data->convertible = storage;
    }
};

void bind_entry()
{
    to_python_converter<boost::shared_ptr<libtorrent::entry>, entry_to_python>();
    to_python_converter<entry, entry_to_python>();
    entry_from_python();
}

