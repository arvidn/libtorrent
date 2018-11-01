// Copyright Daniel Wallin 2007. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef GIL_070107_HPP
# define GIL_070107_HPP

#include <libtorrent/aux_/disable_warnings_push.hpp>

# include <boost/python/make_function.hpp>
# include <boost/python/def_visitor.hpp>
# include <boost/python/signature.hpp>
# include <boost/mpl/at.hpp>

#include <libtorrent/aux_/disable_warnings_pop.hpp>

//namespace libtorrent { namespace python {

// RAII helper to release GIL.
struct allow_threading_guard
{
    allow_threading_guard() : save(PyEval_SaveThread()) {}
    ~allow_threading_guard() { PyEval_RestoreThread(save); }
    PyThreadState* save;
};

struct lock_gil
{
  lock_gil() : state(PyGILState_Ensure()) {}
  ~lock_gil() { PyGILState_Release(state); }
  PyGILState_STATE state;
};

template <class F, class R>
struct allow_threading
{
    allow_threading(F fn) : fn(fn) {}
    template <typename Self, typename... Args>
    R operator()(Self&& s, Args&&... args)
    {
        allow_threading_guard guard;
        return (std::forward<Self>(s).*fn)(std::forward<Args>(args)...);
    }
    F fn;
};

template <class F>
struct visitor : boost::python::def_visitor<visitor<F>>
{
    visitor(F fn) : fn(fn) {}

    template <class Class, class Options, class Signature>
    void visit_aux(
        Class& cl, char const* name
      , Options const& options, Signature const& signature) const
    {
        typedef typename boost::mpl::at_c<Signature,0>::type return_type;

        cl.def(
            name
          , boost::python::make_function(
                allow_threading<F, return_type>(fn)
              , options.policies()
              , options.keywords()
              , signature
            )
        );
    }

    template <class Class, class Options>
    void visit(Class& cl, char const* name, Options const& options) const
    {
        this->visit_aux(
            cl, name, options
          , boost::python::detail::get_signature(fn, (typename Class::wrapped_type*)0)
        );
    }

    F fn;
};

// Member function adaptor that releases and aqcuires the GIL
// around the function call.
template <class F>
visitor<F> allow_threads(F fn)
{
    return visitor<F>(fn);
}

//}} // namespace libtorrent::python

#endif // GIL_070107_HPP
