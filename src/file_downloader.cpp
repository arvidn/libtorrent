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
#include "libtorrent/extensions.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/peer_id.hpp" // for sha1_hash
#include "libtorrent/alert_types.hpp"
#include "libtorrent/torrent.hpp"

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

	struct piece_alert_dispatch : plugin
	{
		void on_alert(alert const* a)
		{
			read_piece_alert const* p = alert_cast<read_piece_alert>(a);
			if (p == NULL) return;

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

	file_downloader::file_downloader(session& s)
		: m_ses(s)
		, m_dispatch(new piece_alert_dispatch())
		, m_queue_size(4 * 1024 * 1024)
	{
		m_ses.add_extension(boost::static_pointer_cast<plugin>(m_dispatch));
	}

	bool file_downloader::handle_http(mg_connection* conn,
		mg_request_info const* request_info)
	{
		if (!string_begins_no_case(request_info->uri, "/download")
			&& !string_begins_no_case(request_info->uri, "/proxy"))
			return false;

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
				char const* divider = strchr(range, '-');
				if (divider)
				{
					range_first_byte = strtoll(range, NULL, 10);
					range_last_byte = strtoll(divider+1, NULL, 10);
					range_request = true;
					printf("range: %"PRId64" - %"PRId64"\n", range_first_byte, range_last_byte);
				}
			}
		}

		peer_request req = ti.map_file(file, range_first_byte, 0);
		int first_piece = req.piece;
		int end_piece = ti.map_file(file, range_last_byte, 0).piece + 1;
		boost::uint64_t offset = req.start;

		torrent_piece_queue pq;
		pq.begin = first_piece;
		pq.finish = end_piece;
		pq.end = (std::min)(first_piece + (std::max)(m_queue_size / ti.piece_length(), 1), pq.finish);

		m_dispatch->subscribe(info_hash, &pq);

		int priority_cursor = pq.begin;

		if (range_request && (range_first_byte > range_last_byte
			|| range_last_byte >= file_size
			|| range_first_byte < 0))
		{
			mg_printf(conn, "HTTP/1.1 416 Requested Range Not Satisfiable\r\n"
				"Content-Length: %" PRId64 "\r\n\r\n"
				, file_size);
			return true;
		}

		mg_printf(conn, "HTTP/1.1 %s\r\n"
			"Content-Length: %" PRId64 "\r\n"
			"Content-Type: %s\r\n"
			"Content-Disposition: attachment; filename=%s\r\n"
			"Accept-Ranges: bytes\r\n"
			, range_request ? "206 Partial Content" : "200 OK"
			, range_last_byte - range_first_byte + 1
			, mg_get_builtin_mime_type(ti.files().file_name(file).c_str())
			// TODO: make sure to escape any new-line characters in the filename
			, ti.files().file_name(file).c_str());

		if (range_request)
		{
			mg_printf(conn, "Content-Range: bytes %"PRId64"-%"PRId64"/%"PRId64"\r\n\r\n"
				, range_first_byte, range_last_byte, file_size);
		}
		else
		{
			mg_printf(conn, "\r\n");
		}

		offset += range_first_byte;
		boost::int64_t left_to_send = range_last_byte - range_first_byte + 1;


		for (int i = pq.begin; i < pq.finish; ++i)
		{
			while (priority_cursor < pq.end)
			{
//				printf("set_piece_deadline: %d\n", priority_cursor);
				h.set_piece_deadline(priority_cursor
					, 100 * (priority_cursor - pq.begin)
					, torrent_handle::alert_when_available);
				++priority_cursor;
			}

			mutex::scoped_lock l(pq.queue_mutex);

			// TODO: come up with some way to abort
			while (pq.queue.empty() || pq.queue.top().piece != i)
				pq.cond.wait(l);

			piece_entry pe = pq.queue.top();
			pq.queue.pop();
			pq.end = (std::min)(pq.end + 1, pq.finish);
			pq.begin = (std::min)(pq.begin + 1, pq.end);

			l.unlock();

			int amount_to_send = (std::min)(boost::int64_t(pe.size - offset), left_to_send);
			int ret = mg_write(conn, &pe.buffer[offset], amount_to_send);
			if (ret != amount_to_send)
			{
				printf("interrupted\n");
				break;
			}

			left_to_send -= amount_to_send;
			printf("sent: %d bytes\n", amount_to_send);
			offset = 0;
		}

		m_dispatch->unsubscribe(info_hash, &pq);
		printf("done\n");
		return true;
	}
}

