SUBDIRS = include @ZLIBDIR@ src examples test
EXTRA_DIST = docs/manual.html docs/manual.rst docs/extension_protocol.rst \
docs/extension_protocol.html docs/udp_tracker_protocol.rst \
docs/udp_tracker_protocol.html docs/client_test.rst docs/client_test.html \
docs/unicode_support.png docs/client_test.png docs/style.css Jamfile project-root.jam \
m4/ac_cxx_namespaces.m4 m4/acx_pthread.m4 m4/ax_boost_date-time.m4 \
m4/ax_boost_filesystem.m4 m4/ax_boost_thread.m4 src/file_win.cpp libtorrent.pc

pkginclude_HEADER = \
debian/changelog \
debian/compat \
debian/control \
debian/copyright \
debian/files \
debian/libtorrent0-dev.dirs \
debian/libtorrent0-dev.docs \
debian/libtorrent0-dev.install \
debian/libtorrent0.dirs \
debian/libtorrent0.docs \
debian/libtorrent0.install \
debian/rules \
asio/aclocal.m4 \
asio/autogen.sh \
asio/boostify.pl \
asio/configure \
asio/configure.ac \
asio/COPYING \
asio/depcomp \
asio/include/asio/basic_datagram_socket.hpp \
asio/include/asio/basic_deadline_timer.hpp \
asio/include/asio/basic_io_object.hpp \
asio/include/asio/basic_resolver.hpp \
asio/include/asio/basic_socket.hpp \
asio/include/asio/basic_socket_acceptor.hpp \
asio/include/asio/basic_strand.hpp \
asio/include/asio/basic_stream_socket.hpp \
asio/include/asio/buffer.hpp \
asio/include/asio/buffered_read_stream.hpp \
asio/include/asio/buffered_read_stream_fwd.hpp \
asio/include/asio/buffered_stream.hpp \
asio/include/asio/buffered_stream_fwd.hpp \
asio/include/asio/buffered_write_stream.hpp \
asio/include/asio/buffered_write_stream_fwd.hpp \
asio/include/asio/completion_condition.hpp \
asio/include/asio/datagram_socket_service.hpp \
asio/include/asio/deadline_timer.hpp \
asio/include/asio/deadline_timer_service.hpp \
asio/include/asio/detail/bind_handler.hpp \
asio/include/asio/detail/buffer_resize_guard.hpp \
asio/include/asio/detail/buffered_stream_storage.hpp \
asio/include/asio/detail/call_stack.hpp \
asio/include/asio/detail/consuming_buffers.hpp \
asio/include/asio/detail/epoll_reactor.hpp \
asio/include/asio/detail/epoll_reactor_fwd.hpp \
asio/include/asio/detail/event.hpp \
asio/include/asio/detail/fd_set_adapter.hpp \
asio/include/asio/detail/handler_alloc_helpers.hpp \
asio/include/asio/detail/hash_map.hpp \
asio/include/asio/detail/io_control.hpp \
asio/include/asio/detail/kqueue_reactor.hpp \
asio/include/asio/detail/kqueue_reactor_fwd.hpp \
asio/include/asio/detail/mutex.hpp \
asio/include/asio/detail/noncopyable.hpp \
asio/include/asio/detail/null_event.hpp \
asio/include/asio/detail/null_mutex.hpp \
asio/include/asio/detail/null_signal_blocker.hpp \
asio/include/asio/detail/null_thread.hpp \
asio/include/asio/detail/null_tss_ptr.hpp \
asio/include/asio/detail/old_win_sdk_compat.hpp \
asio/include/asio/detail/pipe_select_interrupter.hpp \
asio/include/asio/detail/pop_options.hpp \
asio/include/asio/detail/posix_event.hpp \
asio/include/asio/detail/posix_mutex.hpp \
asio/include/asio/detail/posix_signal_blocker.hpp \
asio/include/asio/detail/posix_thread.hpp \
asio/include/asio/detail/posix_tss_ptr.hpp \
asio/include/asio/detail/push_options.hpp \
asio/include/asio/detail/reactive_deadline_timer_service.hpp \
asio/include/asio/detail/reactive_socket_service.hpp \
asio/include/asio/detail/reactor_op_queue.hpp \
asio/include/asio/detail/reactor_timer_queue.hpp \
asio/include/asio/detail/resolver_service.hpp \
asio/include/asio/detail/scoped_lock.hpp \
asio/include/asio/detail/select_interrupter.hpp \
asio/include/asio/detail/select_reactor.hpp \
asio/include/asio/detail/select_reactor_fwd.hpp \
asio/include/asio/detail/service_registry.hpp \
asio/include/asio/detail/signal_blocker.hpp \
asio/include/asio/detail/signal_init.hpp \
asio/include/asio/detail/socket_holder.hpp \
asio/include/asio/detail/socket_ops.hpp \
asio/include/asio/detail/socket_option.hpp \
asio/include/asio/detail/socket_select_interrupter.hpp \
asio/include/asio/detail/socket_types.hpp \
asio/include/asio/detail/strand_service.hpp \
asio/include/asio/detail/task_io_service.hpp \
asio/include/asio/detail/task_io_service_fwd.hpp \
asio/include/asio/detail/thread.hpp \
asio/include/asio/detail/tss_ptr.hpp \
asio/include/asio/detail/win_event.hpp \
asio/include/asio/detail/win_iocp_io_service.hpp \
asio/include/asio/detail/win_iocp_io_service_fwd.hpp \
asio/include/asio/detail/win_iocp_operation.hpp \
asio/include/asio/detail/win_iocp_socket_service.hpp \
asio/include/asio/detail/win_local_free_on_block_exit.hpp \
asio/include/asio/detail/win_mutex.hpp \
asio/include/asio/detail/win_signal_blocker.hpp \
asio/include/asio/detail/win_thread.hpp \
asio/include/asio/detail/win_tss_ptr.hpp \
asio/include/asio/detail/winsock_init.hpp \
asio/include/asio/detail/wrapped_handler.hpp \
asio/include/asio/error.hpp \
asio/include/asio/error_handler.hpp \
asio/include/asio/handler_alloc_hook.hpp \
asio/include/asio/impl/basic_datagram_socket.ipp \
asio/include/asio/impl/io_service.ipp \
asio/include/asio/impl/read.ipp \
asio/include/asio/impl/write.ipp \
asio/include/asio/io_service.hpp \
asio/include/asio/ip/address.hpp \
asio/include/asio/ip/address_v4.hpp \
asio/include/asio/ip/address_v6.hpp \
asio/include/asio/ip/basic_endpoint.hpp \
asio/include/asio/ip/basic_resolver_entry.hpp \
asio/include/asio/ip/basic_resolver_iterator.hpp \
asio/include/asio/ip/basic_resolver_query.hpp \
asio/include/asio/ip/detail/socket_option.hpp \
asio/include/asio/ip/host_name.hpp \
asio/include/asio/ip/multicast.hpp \
asio/include/asio/ip/resolver_query_base.hpp \
asio/include/asio/ip/tcp.hpp \
asio/include/asio/ip/udp.hpp \
asio/include/asio/is_read_buffered.hpp \
asio/include/asio/is_write_buffered.hpp \
asio/include/asio/placeholders.hpp \
asio/include/asio/read.hpp \
asio/include/asio/resolver_service.hpp \
asio/include/asio/socket_acceptor_service.hpp \
asio/include/asio/socket_base.hpp \
asio/include/asio/ssl/basic_context.hpp \
asio/include/asio/ssl/context.hpp \
asio/include/asio/ssl/context_base.hpp \
asio/include/asio/ssl/context_service.hpp \
asio/include/asio/ssl/detail/openssl_context_service.hpp \
asio/include/asio/ssl/detail/openssl_init.hpp \
asio/include/asio/ssl/detail/openssl_operation.hpp \
asio/include/asio/ssl/detail/openssl_stream_service.hpp \
asio/include/asio/ssl/detail/openssl_types.hpp \
asio/include/asio/ssl/stream.hpp \
asio/include/asio/ssl/stream_base.hpp \
asio/include/asio/ssl/stream_service.hpp \
asio/include/asio/ssl.hpp \
asio/include/asio/strand.hpp \
asio/include/asio/strand_service.hpp \
asio/include/asio/stream_socket_service.hpp \
asio/include/asio/system_exception.hpp \
asio/include/asio/thread.hpp \
asio/include/asio/time_traits.hpp \
asio/include/asio/write.hpp \
asio/include/asio.hpp \
asio/include/Makefile.am \
asio/INSTALL \
asio/install-sh \
asio/LICENSE_1_0.txt \
asio/Makefile.am \
asio/missing \
asio/README


pkgconfigdir   = $(libdir)/pkgconfig
pkgconfig_DATA = libtorrent.pc

check: test
	test/test_hasher \
	&& test/test_bencoding \
	&& test/test_ip_filter \
	&& test/test_piece_picker \
	&& test/test_storage \
	&& test/test_metadata_extension \
	&& test/test_buffer \
	&& echo && echo && echo "  **** all tests passed ****" && echo && echo

deb:
	dpkg-buildpackage -rfakeroot -us -uc
