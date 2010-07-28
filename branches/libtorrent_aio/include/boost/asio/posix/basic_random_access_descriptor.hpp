// Copyright Daniel Wallin & Arvid Norberg 2010. Use, modification and distribution is
// subject to the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_ASIO_BASIC_RANDOM_ACCESS_DESCRIPTOR_100223_HPP
# define BOOST_ASIO_BASIC_RANDOM_ACCESS_DESCRIPTOR_100223_HPP

# include <boost/asio/detail/push_options.hpp>
# include <boost/config.hpp>

# include <boost/asio/error.hpp>
# include <boost/asio/posix/basic_descriptor.hpp>
# include <boost/asio/posix/random_access_descriptor_service.hpp>
# include <boost/asio/detail/throw_error.hpp>

namespace boost { namespace asio { namespace posix {

template <
    class RandomAccessDescriptorService = random_access_descriptor_service
>
class basic_random_access_descriptor
  : public basic_descriptor<RandomAccessDescriptorService>
{
public:
    typedef typename RandomAccessDescriptorService::native_type native_type;

    explicit basic_random_access_descriptor(
        boost::asio::io_service& io_service
    )
      : basic_descriptor<RandomAccessDescriptorService>(io_service)
    {}

    basic_random_access_descriptor(
        boost::asio::io_service& io_service, native_type const& native
    )
      : basic_descriptor<RandomAccessDescriptorService>(io_service, native)
    {}

    /// Get the native handle representation.
    /**
     * This function may be used to obtain the underlying representation of the
     * handle. This is intended to allow access to native handle functionality
     * that is not otherwise provided.
     */
    native_type native()
    {
        return this->service.native(this->implementation);
    }

    /// Determine whether the handle is open.
    bool is_open() const
    {
        return this->service.is_open(this->implementation);
    }

    /// Close the handle.
    /**
     * This function is used to close the handle. Any asynchronous read or write
     * operations will be cancelled immediately, and will complete with the
     * boost::asio::error::operation_aborted error.
     *
     * @throws boost::system::system_error Thrown on failure.
     */
    void close()
    {
        boost::system::error_code ec;
        this->service.close(this->implementation, ec);
        boost::asio::detail::throw_error(ec);
    }

    /// Close the handle.
    /**
     * This function is used to close the handle. Any asynchronous read or write
     * operations will be cancelled immediately, and will complete with the
     * boost::asio::error::operation_aborted error.
     *
     * @param ec Set to indicate what error occurred, if any.
     */
    void close(boost::system::error_code& ec)
    {
        this->service.close(this->implementation, ec);
    }

    template <class MutableBufferSequence>
    std::size_t read_some_at(
        boost::uint64_t offset, MutableBufferSequence const& buffers)
    {
        boost::system::error_code ec;
        std::size_t result = read_some_at(offset, buffers, ec);;
        asio::detail::throw_error(ec);
        return result;
    }

    template <class MutableBufferSequence>
    std::size_t read_some_at(
        boost::uint64_t offset, MutableBufferSequence const& buffers
      , boost::system::error_code& ec)
    {
        return this->service.read_some_at(
            this->implementation, offset, buffers, ec);
    }

    template <class MutableBufferSequence, class ReadHandler>
    void async_read_some_at(
        boost::uint64_t offset
      , MutableBufferSequence const& buffers
      , ReadHandler handler
    )
    {
        this->service.async_read_some_at(
            this->implementation, offset, buffers, handler);
    }

    template <class ConstBufferSequence>
    std::size_t write_some_at(
        boost::uint64_t offset, ConstBufferSequence const& buffers)
    {
        boost::system::error_code ec;
        std::size_t result = write_some_at(offset, buffers, ec);;
        asio::detail::throw_error(ec);
        return result;
    }

    template <class ConstBufferSequence>
    std::size_t write_some_at(
        boost::uint64_t offset, ConstBufferSequence const& buffers
      , boost::system::error_code& ec)
    {
        return this->service.write_some_at(
            this->implementation, offset, buffers, ec);
    }

    template <class ConstBufferSequence, class WriteHandler>
    void async_write_some_at(
        boost::uint64_t offset
      , ConstBufferSequence const& buffers
      , WriteHandler handler
    )
    {
        this->service.async_write_some_at(
            this->implementation, offset, buffers, handler);
    }
};

}}} // namespace boost::asio::posix

# include <boost/asio/detail/pop_options.hpp>

#endif // BOOST_ASIO_BASIC_RANDOM_ACCESS_DESCRIPTOR_100223_HPP
