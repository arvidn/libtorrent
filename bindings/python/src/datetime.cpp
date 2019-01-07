// Copyright Daniel Wallin 2006. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "boost_python.hpp"
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include "optional.hpp"
#include <boost/version.hpp>
#include "libtorrent/time.hpp"
#include <ctime>

using namespace boost::python;

object datetime_timedelta;
object datetime_datetime;

template <typename Duration>
struct chrono_duration_to_python
{
    static PyObject* convert(Duration const& d)
    {
        std::int64_t const us = lt::total_microseconds(d);
        object result = datetime_timedelta(
            0 // days
          , us / 1000000 // seconds
          , us % 1000000 // microseconds
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

template <typename Tag> struct tag {};

lt::time_point now(::tag<lt::time_point>)
{ return lt::clock_type::now(); }

lt::time_point32 now(::tag<lt::time_point32>)
{ return lt::time_point_cast<lt::seconds32>(lt::clock_type::now()); }

template <typename T>
struct time_point_to_python
{
    static PyObject* convert(T const pt)
    {
        using std::chrono::system_clock;
        using std::chrono::duration_cast;
        object result;
        if (pt > T())
        {
           time_t const tm = system_clock::to_time_t(system_clock::now()
              + duration_cast<system_clock::duration>(pt - now(::tag<T>())));

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
        else
        {
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

    to_python_converter<boost::posix_time::time_duration
      , time_duration_to_python>();

    to_python_converter<boost::posix_time::ptime
      , ptime_to_python>();

    to_python_converter<lt::time_point
      , time_point_to_python<lt::time_point>>();

    to_python_converter<lt::time_point32
      , time_point_to_python<lt::time_point32>>();

    to_python_converter<lt::time_duration
      , chrono_duration_to_python<lt::time_duration>>();

    to_python_converter<lt::seconds32
      , chrono_duration_to_python<lt::seconds32>>();

    to_python_converter<std::chrono::seconds
      , chrono_duration_to_python<std::chrono::seconds>>();

    optional_to_python<boost::posix_time::ptime>();
    optional_to_python<std::time_t>();
}

