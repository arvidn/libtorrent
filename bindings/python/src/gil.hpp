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
#include <functional>

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
    visitor(F fn) : fn(std::move(fn)) {}

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

template<typename Fn, typename Self, typename... Args,
    typename std::enable_if<std::is_member_function_pointer<typename std::decay<Fn>::type>::value, int>::type = 0>
auto invoke(Fn&& fn, Self&& s, Args&&... args) ->
#if TORRENT_AUTO_RETURN_TYPES
    decltype(auto)
#else
    decltype((std::forward<Self>(s).*std::forward<Fn>(fn))(std::forward<Args>(args)...))
#endif
{
    return (std::forward<Self>(s).*std::forward<Fn>(fn))(std::forward<Args>(args)...);
}

template<typename Fn, typename Self,
    typename std::enable_if<std::is_member_object_pointer<typename std::decay<Fn>::type>::value, int>::type = 0>
auto invoke(Fn&& fn, Self&& s) ->
#if TORRENT_AUTO_RETURN_TYPES
    decltype(auto)
#else
    decltype((std::forward<Self>(s).*std::forward<Fn>)(fn))
#endif
{
    return (std::forward<Self>(s).*std::forward<Fn>)(fn);
}

template<typename Fn, typename... Args,
    typename std::enable_if<!std::is_member_pointer<typename std::decay<Fn>::type>::value, int>::type = 0>
auto invoke(Fn&& fn, Args&&... args) ->
#if TORRENT_AUTO_RETURN_TYPES
    decltype(auto)
#else
    decltype(std::forward<Fn>(fn)(std::forward<Args>(args)...))
#endif
{
    return std::forward<Fn>(fn)(std::forward<Args>(args)...);
}

template <typename F, typename R>
struct deprecated_fun
{
    deprecated_fun(F fn, char const* name) : fn(fn), fn_name(name) {}
    template <typename... Args>
    R operator()(Args&&... args)
    {
        std::string const msg = std::string(fn_name) + "() is deprecated";
        python_deprecated(msg.c_str());
        // TODO: in C++17 use std::invoke
        return ::invoke(fn, std::forward<Args>(args)...);
    }
    F fn;
    char const* fn_name;
};

template <typename F>
struct deprecate_visitor : boost::python::def_visitor<deprecate_visitor<F>>
{
    deprecate_visitor(F fn) : fn(std::move(fn)) {}

    template <typename Class, typename Options, typename Signature>
    void visit_aux(
        Class& cl, char const* name
      , Options const& options, Signature const& signature) const
    {
        using return_type = typename boost::mpl::at_c<Signature,0>::type;

        cl.def(
            name
          , boost::python::make_function(
                deprecated_fun<F, return_type>(fn, name)
              , options.policies()
              , options.keywords()
              , signature
            )
        );
    }

    template <typename Class, typename Options>
    void visit(Class& cl, char const* name, Options const& options) const
    {
        this->visit_aux(
            cl, name, options
          , boost::python::detail::get_signature(fn, (typename Class::wrapped_type*)0)
        );
    }

    F fn;
};

template <typename F>
deprecate_visitor<F> depr(F fn)
{
    return deprecate_visitor<F>(std::move(fn));
}

//}} // namespace libtorrent::python

#endif // GIL_070107_HPP
