
#if !defined BOOST_PP_IS_ITERATING || !BOOST_PP_IS_ITERATING
// set-up iteration

#include <boost/preprocessor/repetition/enum_params.hpp>
#include <boost/preprocessor/iteration/iterate.hpp>
#include <boost/preprocessor/punctuation/comma_if.hpp>
#include <boost/preprocessor/repetition/enum_binary_params.hpp>

#define BOOST_PP_ITERATION_PARAMS_1 \
	(3, (0, TORRENT_ALERT_MANAGER_MAX_ARITY, \
	"libtorrent/aux_/alert_manager_variadic_emplace.hpp"))
#include BOOST_PP_ITERATE()


#else // BOOST_PP_IS_ITERATING

// loop body

#define I BOOST_PP_ITERATION()

		template <class T
			BOOST_PP_COMMA_IF(I)
			BOOST_PP_ENUM_PARAMS(I, typename A)>
		bool emplace_alert(BOOST_PP_ENUM_BINARY_PARAMS(I, A, const& a) )
		{
			// allocate thread specific storage the first time the thread runs
			// this will be freed automagically by boost::thread_specific_ptr
			if (m_allocations[0].get() == NULL)
				init_thread_specific_storage();

			const int gen = (*m_generation).load();

#ifndef TORRENT_NO_DEPRECATE
			if (m_dispatch.load(boost::memory_order_relaxed))
			{
				aux::stack_allocator::scoped_lock lock(*m_allocations[gen]);
				(*m_dispatch.load(boost::memory_order_relaxed))(std::auto_ptr<alert>(new T(lock.allocator()
					BOOST_PP_COMMA_IF(I)
					BOOST_PP_ENUM_PARAMS(I, a))));
				return false;
			}
#endif

			// don't add more than this number of alerts, unless it's a
			// high priority alert, in which case we try harder to deliver it
			// for high priority alerts, double the upper limit
			if (m_queue_size.load(boost::memory_order_relaxed) >=
				(m_queue_size_limit.load(boost::memory_order_relaxed) * (1 + T::priority)))
			{
#ifndef TORRENT_DISABLE_EXTENSIONS
				if (!m_ses_extensions_reliable.empty())
				{
					aux::stack_allocator::scoped_lock lock(*m_allocations[gen]);
					T alert(lock.allocator()
						BOOST_PP_COMMA_IF(I)
						BOOST_PP_ENUM_PARAMS(I, a));

					notify_extensions(&alert, m_ses_extensions_reliable);
				}
#endif
				return false;
			}

			do
			{
				bool aborted;
				T* alert = new T(*m_allocations[gen]
					BOOST_PP_COMMA_IF(I)
					BOOST_PP_ENUM_PARAMS(I, a));

				if (!do_emplace_alert(alert, T::priority, gen, aborted))
				{
					// free the alert
					delete alert;

					// this is just a safety net in case alerts where
					// popped twice while do_emplace_alert() was running.
					if (aborted) continue;

#ifndef TORRENT_DISABLE_EXTENSIONS
					if (!m_ses_extensions_reliable.empty())
					{
						aux::stack_allocator::scoped_lock lock(*m_allocations[gen]);
						T alert(lock.allocator()
							BOOST_PP_COMMA_IF(I)
							BOOST_PP_ENUM_PARAMS(I, a));

						notify_extensions(&alert, m_ses_extensions_reliable);
					}
#endif
					return false;
				}
				return true;
			}
			while (1);
		}

#undef I

#endif

