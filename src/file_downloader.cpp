/*

Copyright (c) 2012, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include "webui.hpp"
#include "file_downloader.hpp"
#include "no_auth.hpp"
#include "auth.hpp"

#include "libtorrent/session.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/peer_id.hpp" // for sha1_hash
#include "libtorrent/alert_types.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/escape_string.hpp" // for escape_string

#include <boost/shared_array.hpp>
#include <map>
#include <queue>

extern "C" {
#include "local_mongoose.h"
}

namespace libtorrent
{
	struct piece_entry
	{
		boost::shared_array<char> buffer;
		int size;
		int piece;
		// we want ascending order!
		bool operator<(piece_entry const& rhs) const { return piece > rhs.piece; }
	};

	struct torrent_piece_queue
	{
		// this is the range of pieces we're interested in
		int begin;
		int end;
		// end may not progress past this. This is end of file
		// or end of request
		int finish;
		std::priority_queue<piece_entry> queue;
		condition_variable cond;
		mutex queue_mutex;
	};

	struct request_t
	{
		request_t(std::string filename, std::set<request_t*>& list, mutex& m)
			: start_time(time_now())
			, file(filename)
			, request_size(0)
			, file_size(0)
			, start_offset(0)
			, bytes_sent(0)
			, piece(-1)
			, state(0)
			, m_requests(list)
			, m_mutex(m)
		{
			mutex::scoped_lock l(m_mutex);
			m_requests.insert(this);
		}

		~request_t()
		{
			mutex::scoped_lock l(m_mutex);
			debug_print(time_now());
			m_requests.erase(this);
		}

		void debug_print(ptime now) const
		{
			const int progress_width = 150;
			char prefix[progress_width+1];
			char suffix[progress_width+1];
			char progress[progress_width+1];
			char invprogress[progress_width+1];

			memset(prefix, ' ', sizeof(prefix));
			memset(suffix, ' ', sizeof(suffix));
			memset(progress, '#', sizeof(progress));
			memset(invprogress, '.', sizeof(invprogress));

			int start = (std::min)(start_offset * progress_width / file_size, boost::uint64_t(progress_width) - 1);
			int progress_range = (std::max)(boost::uint64_t(1), request_size * progress_width / file_size);
			int e = (start_offset + request_size) * progress_width / file_size;
			int pos = request_size == 0 ? 0 : bytes_sent * progress_range / request_size;
			int pos_end = progress_range - pos;
			prefix[start] = 0;
			progress[pos] = 0;
			invprogress[pos_end] = 0;
			suffix[progress_width-start-pos-pos_end] = 0;

			printf("%4.1f [%s%s%s%s] [p: %4d] [s: %d] %s\n"
				, total_milliseconds(now - start_time) / 1000.f
				, prefix, progress, invprogress, suffix, piece, state, file.c_str());
		}

		enum state_t
		{
			received, writing_to_socket, waiting_for_libtorrent
		};

		const ptime start_time;
		const std::string file;
		boost::uint64_t request_size;
		boost::uint64_t file_size;
		boost::uint64_t start_offset;
		boost::uint64_t bytes_sent;
		int piece;
		int state;

	private:
		std::set<request_t*>& m_requests;
		mutex& m_mutex;
	};

	// TODO: replace this with file_requests class
	struct piece_alert_dispatch : plugin
	{
		void on_alert(alert const* a)
		{
			read_piece_alert const* p = alert_cast<read_piece_alert>(a);
			if (p == NULL) return;

//			fprintf(stderr, "piece: %d\n", p->piece);

			mutex::scoped_lock l(m_mutex);
			typedef std::multimap<sha1_hash, torrent_piece_queue*>::iterator iter;
			boost::shared_ptr<torrent> t = p->handle.native_handle();

			std::pair<iter, iter> range = m_torrents.equal_range(t->info_hash());
			if (range.first == m_torrents.end()) return;

			for (iter i = range.first; i != range.second; ++i)
			{
				mutex::scoped_lock l2(i->second->queue_mutex);
				if (p->piece < i->second->begin || p->piece >= i->second->end)
					continue;
				piece_entry pe;
				pe.buffer = p->buffer;
				pe.piece = p->piece;
				pe.size = p->size;

				i->second->queue.push(pe);
				if (pe.piece == i->second->begin)
					i->second->cond.notify_all();
			}
		}

		void subscribe(sha1_hash const& ih, torrent_piece_queue* pq)
		{
			mutex::scoped_lock l(m_mutex);
			m_torrents.insert(std::make_pair(ih, pq));
		}

		void unsubscribe(sha1_hash const& ih, torrent_piece_queue* pq)
		{
			mutex::scoped_lock l(m_mutex);
			typedef std::multimap<sha1_hash, torrent_piece_queue*>::iterator iter;

			std::pair<iter, iter> range = m_torrents.equal_range(ih);
			if (range.first == m_torrents.end()) return;

			for (iter i = range.first; i != range.second; ++i)
			{
				if (i->second != pq) continue;
				m_torrents.erase(i);
				break;
			}
		}
	
	private:

		mutex m_mutex;
		std::multimap<sha1_hash, torrent_piece_queue*> m_torrents;
	
	};

	file_downloader::file_downloader(session& s, auth_interface const* auth)
		: m_ses(s)
		, m_auth(auth)
		, m_dispatch(new piece_alert_dispatch())
// TODO: this number needs to be proportional to the rate at which a file
// is downloaded
		, m_queue_size(20 * 1024 * 1024)
		, m_attachment(true)
	{
		if (m_auth == NULL)
		{
			const static no_auth n;
			m_auth = &n;
		}

		m_ses.add_extension(boost::static_pointer_cast<plugin>(m_dispatch));
	}

	bool file_downloader::handle_http(mg_connection* conn,
		mg_request_info const* request_info)
	{
		if (!string_begins_no_case(request_info->uri, "/download")
			&& !string_begins_no_case(request_info->uri, "/proxy"))
			return false;

		permissions_interface const* perms = parse_http_auth(conn, m_auth);
		if (!perms || !perms->allow_get_data())
		{
			mg_printf(conn, "HTTP/1.1 401 Unauthorized\r\n"
				"WWW-Authenticate: Basic realm=\"BitTorrent\"\r\n"
				"Content-Length: 0\r\n\r\n");
			return true;
		}

		std::string info_hash_str;
		std::string file_str;
		if (request_info->query_string)
		{
			std::string query_string = "?";
			query_string += request_info->query_string;
			info_hash_str = url_has_argument(query_string, "ih");
			file_str = url_has_argument(query_string, "file");
			if (info_hash_str.empty())
				info_hash_str = url_has_argument(query_string, "sid");
		}

		if (file_str.empty() || info_hash_str.empty() || info_hash_str.size() != 40)
		{
			mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n\r\n");
			return true;
		}

		int file = atoi(file_str.c_str());

		sha1_hash info_hash;
		from_hex(info_hash_str.c_str(), 40, (char*)&info_hash[0]);

		torrent_handle h = m_ses.find_torrent(info_hash);

		// TODO: it would be nice to wait for the metadata to complete
		if (!h.is_valid() || !h.has_metadata())
		{
			mg_printf(conn, "HTTP/1.1 404 Not Found\r\n\r\n");
			return true;
		}

		torrent_info const& ti = h.get_torrent_info();
		if (file < 0 || file >= ti.num_files())
		{
			mg_printf(conn, "HTTP/1.1 400 Bad Request\r\n\r\n");
			return true;
		}

		boost::int64_t file_size = ti.files().file_size(file);
		boost::int64_t range_first_byte = 0;
		boost::int64_t range_last_byte = file_size - 1;
		bool range_request = false;

		char const* range = mg_get_header(conn, "range");
		if (range)
		{
			range = strstr(range, "bytes=");
			if (range)
			{
				range += 6; // skip bytes=
				char const* divider = strchr(range, '-');
				if (divider)
				{
					range_first_byte = strtoll(range, NULL, 10);

					// if the end of a range is not specified, the end of file
					// is implied
					if (divider[1] != '\0')
						range_last_byte = strtoll(divider+1, NULL, 10);
					else
						range_last_byte = file_size - 1;

					range_request = true;
				}
			}
		}

		peer_request req = ti.map_file(file, range_first_byte, 0);
		int piece_size = ti.piece_length();
		int first_piece = req.piece;
		int end_piece = ti.map_file(file, range_last_byte, 0).piece + 1;
		boost::uint64_t offset = req.start;

		if (range_request && (range_first_byte > range_last_byte
			|| range_last_byte >= file_size
			|| range_first_byte < 0))
		{
			mg_printf(conn, "HTTP/1.1 416 Requested Range Not Satisfiable\r\n"
				"Content-Length: %" PRId64 "\r\n\r\n"
				, file_size);
			return true;
		}

		printf("GET range: %" PRId64 " - %" PRId64 "\n", range_first_byte, range_last_byte);

		torrent_piece_queue pq;
		pq.begin = first_piece;
		pq.finish = end_piece;
		pq.end = (std::min)(first_piece + (std::max)(m_queue_size / ti.piece_length(), 1), pq.finish);

		m_dispatch->subscribe(info_hash, &pq);

		int priority_cursor = pq.begin;

		request_t r(ti.files().file_path(file), m_requests, m_mutex);
		r.request_size = range_last_byte - range_first_byte + 1;
		r.file_size = ti.files().file_size(file);
		r.start_offset = range_first_byte;

		std::string fname = ti.files().file_name(file);
		r.state = request_t::writing_to_socket;
		mg_printf(conn, "HTTP/1.1 %s\r\n"
			"Content-Length: %" PRId64 "\r\n"
			"Content-Type: %s\r\n"
			"%s%s%s"
			"Accept-Ranges: bytes\r\n"
			, range_request ? "206 Partial Content" : "200 OK"
			, range_last_byte - range_first_byte + 1
			, mg_get_builtin_mime_type(ti.files().file_name(file).c_str())
			, m_attachment ? "Content-Disposition: attachment; filename=" : ""
			, m_attachment ? escape_string(fname.c_str(), fname.size()).c_str() : ""
			, m_attachment ? "\r\n" : "");

		if (range_request)
		{
			mg_printf(conn, "Content-Range: bytes %" PRId64 "-%" PRId64 "/%" PRId64 "\r\n\r\n"
				, range_first_byte, range_last_byte, file_size);
		}
		else
		{
			mg_printf(conn, "\r\n");
		}
		r.state = request_t::waiting_for_libtorrent;

		boost::int64_t left_to_send = range_last_byte - range_first_byte + 1;
//		printf("left_to_send: %" PRId64 " bytes\n", left_to_send);

		// increase the priority of this range to 5
		std::vector<std::pair<int, int> > pieces_in_req;
		pieces_in_req.resize(pq.finish - pq.begin);
		int p = pq.begin;
		for (int i = 0; i < pieces_in_req.size(); ++i)
		{
			pieces_in_req[i] = std::make_pair(p, 5);
			++p;
		}
		h.prioritize_pieces(pieces_in_req);

		while (priority_cursor < pq.end)
		{
//			printf("set_piece_deadline: %d\n", priority_cursor);
			h.set_piece_deadline(priority_cursor
				, 100 * (priority_cursor - pq.begin)
				, torrent_handle::alert_when_available);
			++priority_cursor;
		}

		for (int i = pq.begin; i < pq.finish; ++i)
		{
			mutex::scoped_lock l(pq.queue_mutex);

			// TODO: come up with some way to abort
			while (pq.queue.empty() || pq.queue.top().piece > i)
				pq.cond.wait(l);

			piece_entry pe = pq.queue.top();
			pq.queue.pop();

			if (pe.piece < i)
			{
				--i; // we don't want to increment i in this case. Just ignore
				// the piece we got in from the queue
				continue;
			}

			pq.end = (std::min)(pq.end + 1, pq.finish);
			pq.begin = (std::min)(pq.begin + 1, pq.end);

			l.unlock();

			while (priority_cursor < pq.end)
			{
//				printf("set_piece_deadline: %d\n", priority_cursor);
				h.set_piece_deadline(priority_cursor
					, 100 * (priority_cursor - i)
					, torrent_handle::alert_when_available);
				++priority_cursor;
			}

			r.piece = pe.piece;

			if (pe.size == 0)
			{
				printf("interrupted (zero bytes read)\n");

				for (int k = i; k < priority_cursor; ++k)
				{
					printf("reset_piece_deadline: %d\n", k);
					h.reset_piece_deadline(k);
				}
				break;
			}

			int ret = -1;
			int amount_to_send = (std::min)(boost::int64_t(pe.size - offset), left_to_send);
//			fprintf(stderr, "[%p] amount_to_send: 0x%x bytes [p: %d] [l: %" PRId64 "]\n"
//				, &r, amount_to_send, pq.finish - i, left_to_send);

			while (amount_to_send > 0)
			{
				r.state = request_t::writing_to_socket;
				TORRENT_ASSERT(offset >= 0);
				TORRENT_ASSERT(offset + amount_to_send <= pe.size);
				ret = mg_write(conn, &pe.buffer[offset], amount_to_send);
				if (ret <= 0)
				{
					fprintf(stderr, "interrupted (%d) errno: (%d) %s\n", ret, errno
						, strerror(errno));
					if (ret < 0 && errno == EAGAIN) {
						usleep(100000);
						continue;
					}
					break;
				}
				TORRENT_ASSERT(r.bytes_sent + ret<= r.request_size);
				r.bytes_sent += ret;
				r.state = request_t::waiting_for_libtorrent;

				left_to_send -= ret;
//				printf("sent: %d bytes [%d]\n", amount_to_send, i);
				offset += ret;
				amount_to_send -= ret;
			}
			if (ret <= 0) break;
			offset = 0;
		}

		m_dispatch->unsubscribe(info_hash, &pq);
//		printf("done, sent %" PRId64 " bytes\n", r.bytes_sent);

		// TODO: this doesn't work right if there are overlapping requests

		// restore piece priorities
		for (int i = 0; i < pieces_in_req.size(); ++i)
			pieces_in_req[i].second = 1;
		h.prioritize_pieces(pieces_in_req);

		return true;
	}

	void file_downloader::debug_print_requests() const
	{
		ptime now = time_now();
		mutex::scoped_lock l(m_mutex);
		for (std::set<request_t*>::const_iterator i = m_requests.begin()
			, end(m_requests.end()); i != end; ++i)
		{
			request_t const& r = **i;
			r.debug_print(now);
		}
	}
}

