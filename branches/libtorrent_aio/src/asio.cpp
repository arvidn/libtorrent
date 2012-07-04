// builds all boost.asio source as a separate compilation unit
#include <boost/version.hpp>

#ifndef BOOST_ASIO_SOURCE
#define BOOST_ASIO_SOURCE
#endif

#ifdef _MSC_VER

// on windows; including timer_queue.hpp results in an
// actual link-time dependency on boost.date_time, even
// though it's never referenced. So, avoid that on windows.
// on Mac OS X and Linux, not including it results in
// missing symbols. For some reason, this current setup
// works, at least across windows, Linux and Mac OS X.
// In the future this hack can be fixed by disabling
// use of boost.date_time in boost.asio

#include <boost/asio/detail/config.hpp>

#if defined(BOOST_ASIO_HEADER_ONLY)
# error Do not compile Asio library source with BOOST_ASIO_HEADER_ONLY defined
#endif

#include <boost/asio/impl/error.ipp>
#include <boost/asio/impl/io_service.ipp>
#include <boost/asio/impl/serial_port_base.ipp>
#include <boost/asio/detail/impl/descriptor_ops.ipp>
#include <boost/asio/detail/impl/dev_poll_reactor.ipp>
#include <boost/asio/detail/impl/epoll_reactor.ipp>
#include <boost/asio/detail/impl/eventfd_select_interrupter.ipp>
#if BOOST_VERSION >= 104700
#include <boost/asio/detail/impl/handler_tracking.ipp>
#include <boost/asio/detail/impl/signal_set_service.ipp>
#include <boost/asio/detail/impl/win_static_mutex.ipp>
#endif
#include <boost/asio/detail/impl/kqueue_reactor.ipp>
#include <boost/asio/detail/impl/pipe_select_interrupter.ipp>
#include <boost/asio/detail/impl/posix_event.ipp>
#include <boost/asio/detail/impl/posix_mutex.ipp>
#include <boost/asio/detail/impl/posix_thread.ipp>
#include <boost/asio/detail/impl/posix_tss_ptr.ipp>
#include <boost/asio/detail/impl/reactive_descriptor_service.ipp>
#include <boost/asio/detail/impl/reactive_serial_port_service.ipp>
#include <boost/asio/detail/impl/reactive_socket_service_base.ipp>
#include <boost/asio/detail/impl/resolver_service_base.ipp>
#include <boost/asio/detail/impl/select_reactor.ipp>
#include <boost/asio/detail/impl/service_registry.ipp>
#include <boost/asio/detail/impl/socket_ops.ipp>
#include <boost/asio/detail/impl/socket_select_interrupter.ipp>
#include <boost/asio/detail/impl/strand_service.ipp>
#include <boost/asio/detail/impl/task_io_service.ipp>
#include <boost/asio/detail/impl/throw_error.ipp>
//#include <boost/asio/detail/impl/timer_queue.ipp>
#include <boost/asio/detail/impl/timer_queue_set.ipp>
#include <boost/asio/detail/impl/win_iocp_handle_service.ipp>
#include <boost/asio/detail/impl/win_iocp_io_service.ipp>
#include <boost/asio/detail/impl/win_iocp_serial_port_service.ipp>
#include <boost/asio/detail/impl/win_iocp_socket_service_base.ipp>
#include <boost/asio/detail/impl/win_event.ipp>
#include <boost/asio/detail/impl/win_mutex.ipp>
#include <boost/asio/detail/impl/win_thread.ipp>
#include <boost/asio/detail/impl/win_tss_ptr.ipp>
#include <boost/asio/detail/impl/winsock_init.ipp>
#include <boost/asio/ip/impl/address.ipp>
#include <boost/asio/ip/impl/address_v4.ipp>
#include <boost/asio/ip/impl/address_v6.ipp>
#include <boost/asio/ip/impl/host_name.ipp>
#include <boost/asio/ip/detail/impl/endpoint.ipp>
#include <boost/asio/local/detail/impl/endpoint.ipp>
#if BOOST_VERSION >= 104900
#include <boost/asio/detail/impl/win_object_handle_service.ipp>
#endif

#else // _MSC_VER

#if BOOST_VERSION >= 104500
#include <boost/asio/impl/src.hpp>
#elif BOOST_VERSION >= 104400
#include <boost/asio/impl/src.cpp>
#endif

#endif
