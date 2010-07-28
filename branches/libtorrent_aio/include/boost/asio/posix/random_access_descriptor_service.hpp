// Copyright Daniel Wallin & Arvid Norberg 2010. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_RANDOM_ACCESS_DESCRIPTOR_SERVICE_100303_HPP
# define BOOST_RANDOM_ACCESS_DESCRIPTOR_SERVICE_100303_HPP

# include <boost/asio/detail/push_options.hpp>
# include <boost/config.hpp>
# include <boost/asio/buffer.hpp>
# include <boost/asio/error.hpp>
# include <boost/asio/io_service.hpp>
# include <boost/asio/detail/service_base.hpp>
# include <boost/asio/detail/socket_types.hpp>
# include <boost/asio/posix/stream_descriptor.hpp>

# define BOOST_ASIO_DISABLE_SIGNALFD

# if !defined(BOOST_ASIO_DISABLE_POSIX_STREAM_DESCRIPTOR)
#  if !defined(BOOST_WINDOWS) && !defined(__CYGWIN__)
#   define BOOST_ASIO_HAS_POSIX_RANDOM_ACCESS_DESCRIPTOR 1
#  endif // !defined(BOOST_WINDOWS) && !defined(__CYGWIN__)
# endif // !defined(BOOST_ASIO_DISABLE_POSIX_STREAM_DESCRIPTOR)

# if defined(BOOST_ASIO_HAS_POSIX_RANDOM_ACCESS_DESCRIPTOR) \
  || defined(GENERATING_DOCUMENTATION)

#  include <boost/bind.hpp>
#  include <boost/static_assert.hpp>

#  include <aio.h>
#  include <signal.h>
#  include <unistd.h>

#  if defined(__linux__) && !defined(BOOST_ASIO_DISABLE_SIGNALFD)
#   if LINUX_VERSION_CODE >= KERNEL_VERSION (2,6,27)
#    define BOOST_ASIO_HAS_SIGNALFD
#   endif
#  endif

#  if defined(BOOST_ASIO_HAS_SIGNALFD)
#   include <sys/signalfd.h>
#  endif

#  if defined(SIGRTMIN)
#   define BOOST_ASIO_POSIX_SIGNAL SIGRTMIN
#  else
#   define BOOST_ASIO_POSIX_SIGNAL SIGIO
#  endif

#  if defined(__mach__)
#   define BOOST_ASIO_NO_SIVAL
#  endif

namespace boost { namespace asio { namespace posix {

class random_access_descriptor_service;

namespace aux
{
  typedef boost::asio::detail::service_base<random_access_descriptor_service>
    random_access_service_base;

  template <class BufferSequence, class Buffer>
  bool first_non_empty_buffer(
      BufferSequence const& buffers, Buffer& buffer)
  {
      typename BufferSequence::const_iterator iter = buffers.begin();
      typename BufferSequence::const_iterator end = buffers.end();

      for (; iter != end; ++iter)
      {
          buffer = Buffer(*iter);
          if (boost::asio::buffer_size(buffer) != 0)
              return true;
      }

      return false;
  }

} // namespace detail

class random_access_descriptor_service
  : public aux::random_access_service_base
{
private:
#  if defined(BOOST_ASIO_HAS_SIGNALFD)
    typedef signalfd_siginfo signal_info_t;
#  else
    // When signalfd() isn't available we write this to a pipe()
    // instead. It shares a member with the same type and name
    // with signalfd_siginfo, so that the completion handler code
    // can remain the same.
    struct signal_info_t
    {
        uintptr_t ssi_ptr;
    };

    BOOST_STATIC_ASSERT(sizeof(signal_info_t) < PIPE_BUF);
#  endif

public:
    /// The type of a stream descriptor implementation.
    struct implementation_type
    {
        implementation_type()
          : fd(-1)
        {}

        int fd;
    };

    /// The native descriptor type.
    typedef int native_type;

    random_access_descriptor_service(boost::asio::io_service& io_service)
      : aux::random_access_service_base(io_service)
      , sigfd_(io_service)
      , active_operations_(0)
#  ifdef BOOST_ASIO_NO_SIVAL
      , handlers_(0)
#  endif
    {
#  if defined(BOOST_ASIO_HAS_SIGNALFD)
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, BOOST_ASIO_POSIX_SIGNAL);

        if (pthread_sigmask(SIG_BLOCK, &mask, 0) == -1)
        {
            boost::throw_exception(
                boost::system::system_error(
                    boost::system::error_code(
                        errno, asio::error::get_system_category()
                    )
                  , "sigprocmask"
                )
            );
        }

        sigfd_.assign(signalfd(-1, &mask, 0));

        descriptor_base::non_blocking_io command(true);
        sigfd_.io_control(command);

#  else // BOOST_ASIO_HAS_SIGNALFD

        int sigpipe[2];

        if (::pipe(sigpipe) == -1)
        {
            boost::throw_exception(
                boost::system::system_error(
                    boost::system::error_code(
                        errno, asio::error::get_system_category()
                    )
                  , "pipe"
                )
            );
        }

        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, BOOST_ASIO_POSIX_SIGNAL);

        if (pthread_sigmask(SIG_UNBLOCK, &mask, 0) == -1)
        {
            boost::throw_exception(
                boost::system::system_error(
                    boost::system::error_code(
                        errno, asio::error::get_system_category()
                    )
                  , "sigprocmask"
                )
            );
        }

        sigfd_.assign(sigpipe[0]);
        pipe_write_ = sigpipe[1];

        descriptor_base::non_blocking_io command(true);
        sigfd_.io_control(command);

        // Don't block if we are writing to a full pipe. We need
        // to handle this somehow.
        ::fcntl(pipe_write_, F_SETFL, O_NONBLOCK);

        struct sigaction sa;

        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sa.sa_sigaction = &signal_handler;
        sigemptyset(&sa.sa_mask);

        if (sigaction(BOOST_ASIO_POSIX_SIGNAL, &sa, 0) == -1)
        {
            ::close(pipe_write_);

            boost::throw_exception(
                boost::system::system_error(
                    boost::system::error_code(
                        errno, asio::error::get_system_category()
                    )
                  , "sigaction"
                )
            );
        }

#endif
    }

    void shutdown_service()
    {
#  if !defined(BOOST_ASIO_HAS_SIGNALFD)
        ::close(pipe_write_);
#  endif
    }

    void construct(implementation_type& impl)
    {
        impl.fd = -1;
    }

    void destroy(implementation_type& impl)
    {
        boost::system::error_code ignored_fc;
        asio::detail::descriptor_ops::close(impl.fd, ignored_fc);
    }

    void close(implementation_type& impl, boost::system::error_code& ec)
    {
        asio::detail::descriptor_ops::close(impl.fd, ec);
    }

    native_type native(implementation_type& impl)
    {
        return impl.fd;
    }

    boost::system::error_code assign(
        implementation_type& impl
      , native_type const& native_descriptor, boost::system::error_code& ec)
    {
        impl.fd = native_descriptor;
        return boost::system::error_code();
    }

    bool is_open(implementation_type const& impl)
    {
        return impl.fd != -1;
    }

    boost::system::error_code cancel(
        implementation_type& impl, boost::system::error_code& ec)
    {
        if (!is_open(impl))
        {
            ec = boost::asio::error::bad_descriptor;
        }
        else if (aio_cancel(impl.fd, 0) == -1)
        {
            ec = boost::system::error_code(
                errno, boost::asio::error::get_system_category());
        }

        return ec;
    }

    template <class IoControlCommand>
    boost::system::error_code io_control(
        implementation_type& impl, IoControlCommand& command
      , boost::system::error_code& ec)
    {
        if (!is_open(impl))
        {
            ec = boost::asio::error::bad_descriptor;
            return ec;
        }

        asio::detail::descriptor_ops::ioctl(
            impl.fd, command.name()
          , static_cast<asio::detail::ioctl_arg_type*>(command.data()), ec);
    }

    template <class ConstBufferSequence>
    std::size_t write_some_at(
        implementation_type& impl, boost::uint64_t offset
      , ConstBufferSequence const& buffers, boost::system::error_code& ec)
    {
        if (!is_open(impl))
        {
            ec = boost::asio::error::bad_descriptor;
            return 0;
        }

        boost::asio::const_buffer buffer;

        if (!aux::first_non_empty_buffer(buffers, buffer))
        {
            ec = boost::system::error_code();
            return 0;
        }

        asio::detail::descriptor_ops::clear_error(ec);

        int result = asio::detail::descriptor_ops::error_wrapper(
          ::pwrite(
                impl.fd
              , boost::asio::buffer_cast<void const*>(buffer)
              , boost::asio::buffer_size(buffer)
              , offset
            )
          , ec
        );

        if (result >= 0)
            asio::detail::descriptor_ops::clear_error(ec);

        return result < 0 ? 0 : result;
    }

    template <class ConstBufferSequence, class Handler>
    void async_write_some_at(
        implementation_type& impl, boost::uint64_t offset
      , ConstBufferSequence const& buffers, Handler handler)
    {
        if (!is_open(impl))
        {
            this->get_io_service().post(
                detail::bind_handler(handler, asio::error::bad_descriptor, 0));
            return;
        }

        boost::asio::const_buffer buffer;

        if (!aux::first_non_empty_buffer(buffers, buffer))
        {
            this->get_io_service().post(
                detail::bind_handler(handler, boost::system::error_code(), 0));
            return;
        }

        typedef detail::handler_alloc_traits<
            Handler, completion_handler<Handler> > alloc_traits;
        detail::raw_handler_ptr<alloc_traits> raw_ptr(handler);
        detail::handler_ptr<alloc_traits> ptr(raw_ptr, *this, handler);

        completion_handler_base* h = ptr.get();
        h->aiocb_.aio_fildes = impl.fd;
        h->aiocb_.aio_offset = offset;
        h->aiocb_.aio_buf =
            (void volatile*)asio::buffer_cast<void const*>(buffer);
        h->aiocb_.aio_nbytes = asio::buffer_size(buffer);
        h->aiocb_.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
        h->aiocb_.aio_sigevent.sigev_signo = BOOST_ASIO_POSIX_SIGNAL;
        // darwin does not support forwarding signal context!
        h->aiocb_.aio_sigevent.sigev_value.sival_ptr = h;
        assert(uintptr_t(h->aiocb_.aio_sigevent.sigev_value.sival_int) > 0xffff);
        assert(this != 0);

#ifdef BOOST_ASIO_NO_SIVAL
        // add this handler to the list of all handlers
        h->next_ = handlers_;
        handlers_ = h;
#endif

        boost::system::error_code ec;
        int result = asio::detail::descriptor_ops::error_wrapper(
          ::aio_write(&h->aiocb_), ec);

        if (result != 0)
        {
            this->get_io_service().post(detail::bind_handler(handler, ec, 0));
        }
        else
        {
            ptr.release();

            asio::detail::mutex::scoped_lock lock(mutex_);

            if (active_operations_++ == 0)
                start_aio_completion_handler();
        }
    }

    template <class MutableBufferSequence>
    std::size_t read_some_at(
        implementation_type& impl, boost::uint64_t offset
      , MutableBufferSequence const& buffers, boost::system::error_code& ec)
    {
        if (!is_open(impl))
        {
            ec = boost::asio::error::bad_descriptor;
            return 0;
        }

        boost::asio::mutable_buffer buffer;

        if (!aux::first_non_empty_buffer(buffers, buffer))
        {
            ec = boost::system::error_code();
            return 0;
        }

        int result = asio::detail::descriptor_ops::error_wrapper(
          ::pread(
                impl.fd
              , boost::asio::buffer_cast<void*>(buffer)
              , boost::asio::buffer_size(buffer)
              , offset
            )
          , ec
        );

        if (result < 0)
        {
            return 0;
        }

        if (result == 0)
        {
            ec = boost::asio::error::eof;
        }
        else
        {
            boost::asio::detail::descriptor_ops::clear_error(ec);
        }

        return result;
    }

    template <class MutableBufferSequence, class Handler>
    void async_read_some_at(
        implementation_type& impl, boost::uint64_t offset
      , MutableBufferSequence const& buffers, Handler handler)
    {
        if (!is_open(impl))
        {
            this->get_io_service().post(
                detail::bind_handler(handler, asio::error::bad_descriptor, 0));
            return;
        }

        boost::asio::mutable_buffer buffer;

        if (!aux::first_non_empty_buffer(buffers, buffer))
        {
            this->get_io_service().post(
                detail::bind_handler(handler, boost::system::error_code(), 0));
            return;
        }

        typedef detail::handler_alloc_traits<
            Handler, completion_handler<Handler> > alloc_traits;
        detail::raw_handler_ptr<alloc_traits> raw_ptr(handler);
        detail::handler_ptr<alloc_traits> ptr(raw_ptr, *this, handler);

        completion_handler_base* h = ptr.get();
        h->aiocb_.aio_fildes = impl.fd;
        h->aiocb_.aio_offset = offset;
        h->aiocb_.aio_buf =
            (void volatile*)asio::buffer_cast<void*>(buffer);
        h->aiocb_.aio_nbytes = asio::buffer_size(buffer);
        h->aiocb_.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
        h->aiocb_.aio_sigevent.sigev_signo = BOOST_ASIO_POSIX_SIGNAL;
        // darwin does not support forwarding signal context!
        h->aiocb_.aio_sigevent.sigev_value.sival_ptr = h;
        assert(uintptr_t(h->aiocb_.aio_sigevent.sigev_value.sival_int) > 0xffff);
        assert(this != 0);

#ifdef BOOST_ASIO_NO_SIVAL
        // add this handler to the list of all handlers
        h->next_ = handlers_;
        handlers_ = h;
#endif

        boost::system::error_code ec;
        int result = asio::detail::descriptor_ops::error_wrapper(
          ::aio_read(&ptr.get()->aiocb_), ec);

        if (result != 0)
        {
            this->get_io_service().post(detail::bind_handler(handler, ec, 0));
        }
        else
        {
            ptr.release();

            asio::detail::mutex::scoped_lock lock(mutex_);

            if (active_operations_++ == 0)
                start_aio_completion_handler();
        }
    }


private:
    struct completion_handler_base
    {
        completion_handler_base(random_access_descriptor_service& service_)
          : service_(service_)
          , next_(0)
        {
            memset(&aiocb_, 0, sizeof(aiocb));
        }

        virtual ~completion_handler_base()
        {}

        virtual void complete(
            boost::system::error_code const& ec, std::size_t bytes) = 0;

        random_access_descriptor_service& service_;
#ifdef BOOST_ASIO_NO_SIVAL
        // in case the signal doesn't tell us which handler
        // completed, we need to chain all handlers in a sigly-
        // linked list and traverse it for each signal
        // this pointer is either NULL or points to the next
        // handler in the list
        completion_handler_base* next_;
#endif
        aiocb aiocb_;
    };

    template <class Handler>
    struct completion_handler : completion_handler_base
    {
        typedef completion_handler<Handler> this_type;

        completion_handler(
            random_access_descriptor_service& service_, Handler handler
        )
          : completion_handler_base(service_)
          , handler_(handler)
        {}

        void complete(boost::system::error_code const& ec, std::size_t bytes)
        {
            typedef detail::handler_alloc_traits<Handler, this_type>
                alloc_traits;
            detail::handler_ptr<alloc_traits> ptr(handler_, this);

            // Copy the handler and free the memory before calling
            // into user code.
            Handler handler(handler_);
            ptr.reset();

            boost_asio_handler_invoke_helpers::invoke(
                detail::bind_handler(handler, ec, bytes), &handler);
        }

        Handler handler_;
    };

#  if !defined(BOOST_ASIO_HAS_SIGNALFD)
    static void signal_handler(int, siginfo_t* si, void*)
    {
        if (si->si_signo != BOOST_ASIO_POSIX_SIGNAL) return;

        completion_handler_base* handler =
            static_cast<completion_handler_base*>(si->si_value.sival_ptr);

        signal_info_t siginfo;
        siginfo.ssi_ptr = reinterpret_cast<uintptr_t>(handler);

        for (;;)
        {
            int bytes_written = ::write(
                handler->service_.pipe_write_, &siginfo, sizeof(siginfo));

            if (bytes_written == sizeof(siginfo))
                break;

            // write() on a pipe with buffers with size less than PIPE_BUF is
            // atomic. The only possible failure here should be that the pipe
            // is full. We need to figure out a way to handle that problem,
            // perhaps additional pipes.

            assert(bytes_written == -1 && errno == EAGAIN);
        }
    }
#  endif

    void start_aio_completion_handler()
    {
        sigfd_.async_read_some(
            asio::null_buffers()
          , boost::bind(
                &random_access_descriptor_service::handle_aio_completion
              , this
              , _1
            )
        );
    }

    void handle_aio_completion(boost::system::error_code const& ec)
    {
        if (ec == asio::error::operation_aborted)
            return;

        std::size_t completed_operations = 0;

        // Helper that takes care of restarting the read-op on
        // the signalfd and adjusting active_operation count
        // in case the user handler throws.
        struct readop_restart_guard
        {
            readop_restart_guard(
                random_access_descriptor_service& this_
              , std::size_t const& completed
            )
              : this_(this_)
              , completed(completed)
            {}

            ~readop_restart_guard()
            {
                asio::detail::mutex::scoped_lock lock(this_.mutex_);

                this_.active_operations_ -= completed;

                if (this_.active_operations_ > 0)
                    this_.start_aio_completion_handler();
            }

            random_access_descriptor_service& this_;
            std::size_t const& completed;
        };

        readop_restart_guard guard(*this, completed_operations);

        signal_info_t siginfo;
        boost::system::error_code sig_ec;

        for (;;)
        {
            // We don't techically need to hold the lock while reading from the
            // pipe. asio specifies the operations on shared objects as unsafe,
            // as usual, but the only unsafe thing in the implementation is
            // that there's a race to call ioctl(..., FIONBIO, ...). This
            // doesn't really matter since it's an atomic operation.
            //
            // We could leave the mutex unlocked during this entire
            // loop, and then just lock it afterward to update
            // active_operations and spawn a new reactor operation.

            std::size_t bytes_read = sigfd_.read_some(
                asio::buffer(&siginfo, sizeof(siginfo)), sig_ec);

            if (bytes_read != sizeof(siginfo))
                break;

            ++completed_operations;

            completion_handler_base* ptr =
                reinterpret_cast<completion_handler_base*>(siginfo.ssi_ptr);

#  ifdef BOOST_ASIO_NO_SIVAL
            // darwin does not support passing a sigval along with the signal
            // triggerd by AIO, the effect is that we don't know which aiocb
            // was completed.
            if (ptr == 0)
            {
                completion_handler_base* i = handlers_;
                completion_handler_base* prev = 0;
                while (i)
                {
                    if (aio_error(&i->aiocb_) != EINPROGRESS)
                    {
                        // unlink handler from list
                        if (prev) prev->next_ = i->next_;
                        else handlers_ = i->next_;
                        ptr = i;
                        break;
                    }
                    prev = i;
                    i = i->next_;
                }
                assert(ptr != 0);
            }
#  endif

            // if you hit this assert, you most likely have to rebuild
            // with BOOST_ASIO_NO_SIVAL defined
            assert(ptr != 0);

            assert(aio_error(&ptr->aiocb_) == 0);

            ssize_t result = ::aio_return(&ptr->aiocb_);

            boost::system::error_code aio_ec;

            if (result == -1)
            {
                aio_ec = boost::system::error_code(
                    ::aio_error(&ptr->aiocb_)
                  , asio::error::get_system_category());
                result = 0;
            }
            else if (result == 0)
            {
                aio_ec = asio::error::eof;
            }

            // We can invoke the user supplied handler directly because
            // we are already inside another handler, no need to post().
            //
            // Note that this handler might throw, in which case we need
            // to update active_operations_ and restart the read-op on the
            // signalfd. This is handled by the guard above. If it
            // does throw, we just leave the data in the pipe/signalfd
            // which is fine, because we'll read it when the io_service
            // is restarted.

            ptr->complete(aio_ec, result);
        }
    }

    boost::asio::detail::mutex mutex_;
    stream_descriptor sigfd_;
#  if !defined(BOOST_ASIO_HAS_SIGNALFD)
    int pipe_write_;
#  endif
    std::size_t active_operations_;
#  ifdef BOOST_ASIO_NO_SIVAL
    completion_handler_base* handlers_;
#  endif
};

}}} // namespace boost::asio::posix

# endif // defined(BOOST_ASIO_HAS_POSIX_RANDOM_ACCESS_DESCRIPTOR)
        //   || defined(GENERATING_DOCUMENTATION)

# include <boost/asio/detail/pop_options.hpp>

#endif // BOOST_RANDOM_ACCESS_DESCRIPTOR_SERVICE_100303_HPP
