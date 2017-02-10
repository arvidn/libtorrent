// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include "optional.hpp"
#include <boost/chrono.hpp>
#include <boost/version.hpp>
#include "libtorrent/time.hpp"

using namespace boost::python;
namespace lt = libtorrent;

#if BOOST_VERSION < 103400

// From Boost 1.34
object import(str name)
{
    // should be 'char const *' but older python versions don't use 'const' yet.
    char *n = extract<char *>(name);
    handle<> module(borrowed(PyImport_ImportModule(n)));
    return object(module);
}

#endif

object datetime_timedelta;
object datetime_datetime;

struct chrono_time_duration_to_python
{
    static PyObject* convert(lt::time_duration const& d)
    {
        object result = datetime_timedelta(
            0 // days
          , 0 // seconds
          , lt::total_microseconds(d)
        );

        return incref(result.ptr());
    }
};

struct time_duration_to_python
{
    static PyObject* convert(boost::posix_time::time_duration const& d)
    {
        object result = datetime_timedelta(
            0 // days
          , 0 // seconds
          , d.total_microseconds()
        );

        return incref(result.ptr());
    }
};

#if defined BOOST_ASIO_HAS_STD_CHRONO
using std::chrono::system_clock;
#else
using boost::chrono::system_clock;
#endif

struct time_point_to_python
{
    static PyObject* convert(lt::time_point tpt)
    {
        object result;
        if (tpt > lt::min_time()) {
            time_t const tm = system_clock::to_time_t(system_clock::now()
                + lt::duration_cast<system_clock::duration>(tpt - lt::clock_type::now()));

            std::tm* date = std::localtime(&tm);
            result = datetime_datetime(
                (int)1900 + date->tm_year
                // tm use 0-11 and we need 1-12
                , (int)date->tm_mon + 1
                , (int)date->tm_mday
                , date->tm_hour
                , date->tm_min
                , date->tm_sec
            );
        }
        else {
            result = object();
        }
        return incref(result.ptr());
    }
};

struct ptime_to_python
{
    static PyObject* convert(boost::posix_time::ptime const& pt)
    {
        boost::gregorian::date date = pt.date();
        boost::posix_time::time_duration td = pt.time_of_day();

        object result = datetime_datetime(
            (int)date.year()
          , (int)date.month()
          , (int)date.day()
          , td.hours()
          , td.minutes()
          , td.seconds()
        );

        return incref(result.ptr());
    }
};

void bind_datetime()
{
    object datetime = import("datetime").attr("__dict__");

    datetime_timedelta = datetime["timedelta"];
    datetime_datetime = datetime["datetime"];

    to_python_converter<
        boost::posix_time::time_duration
      , time_duration_to_python
    >();

    to_python_converter<
        lt::time_point
      , time_point_to_python
    >();

    to_python_converter<
        boost::posix_time::ptime
      , ptime_to_python
    >();

    to_python_converter<
        lt::time_duration
      , chrono_time_duration_to_python
    >();

    optional_to_python<boost::posix_time::ptime>();
}

