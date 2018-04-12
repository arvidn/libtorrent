/*

Copyright (c) 2011-2018, Arvid Norberg
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

#include "libtorrent/peer_class.hpp"
#include "libtorrent/peer_connection.hpp"

namespace libtorrent {

	void peer_class::set_upload_limit(int limit)
	{
		TORRENT_ASSERT(limit >= -1);
		if (limit < 0) limit = 0;
		if (limit < 10 && limit > 0) limit = 10;
		channel[peer_connection::upload_channel].throttle(limit);
	}

	void peer_class::set_download_limit(int limit)
	{
		TORRENT_ASSERT(limit >= -1);
		if (limit < 0) limit = 0;
		if (limit < 10 && limit > 0) limit = 10;
		channel[peer_connection::download_channel].throttle(limit);
	}

	void peer_class::get_info(peer_class_info* pci) const
	{
		pci->ignore_unchoke_slots = ignore_unchoke_slots;
		pci->connection_limit_factor = connection_limit_factor;
		pci->label = label;
		pci->upload_limit = channel[peer_connection::upload_channel].throttle();
		pci->download_limit = channel[peer_connection::download_channel].throttle();
		pci->upload_priority = priority[peer_connection::upload_channel];
		pci->download_priority = priority[peer_connection::download_channel];
	}

	void peer_class::set_info(peer_class_info const* pci)
	{
		ignore_unchoke_slots = pci->ignore_unchoke_slots;
		connection_limit_factor = pci->connection_limit_factor;
		label = pci->label;
		set_upload_limit(pci->upload_limit);
		set_download_limit(pci->download_limit);
		priority[peer_connection::upload_channel] = std::max(1, std::min(255, pci->upload_priority));
		priority[peer_connection::download_channel] = std::max(1, std::min(255, pci->download_priority));
	}

	peer_class_t peer_class_pool::new_peer_class(std::string label)
	{
		peer_class_t ret{0};
		if (!m_free_list.empty())
		{
			ret = m_free_list.back();
			m_free_list.pop_back();
			m_peer_classes[ret] = peer_class(std::move(label));
		}
		else
		{
			ret = m_peer_classes.end_index();
			m_peer_classes.emplace_back(std::move(label));
		}

		return ret;
	}

	void peer_class_pool::decref(peer_class_t c)
	{
		TORRENT_ASSERT(c < m_peer_classes.end_index());
		TORRENT_ASSERT(m_peer_classes[c].in_use);
		TORRENT_ASSERT(m_peer_classes[c].references > 0);

		--m_peer_classes[c].references;
		if (m_peer_classes[c].references) return;
		m_peer_classes[c].clear();
		m_free_list.push_back(c);
	}

	void peer_class_pool::incref(peer_class_t c)
	{
		TORRENT_ASSERT(c < m_peer_classes.end_index());
		TORRENT_ASSERT(m_peer_classes[c].in_use);

		++m_peer_classes[c].references;
	}

	peer_class* peer_class_pool::at(peer_class_t c)
	{
		if (c >= m_peer_classes.end_index() || !m_peer_classes[c].in_use) return nullptr;
		return &m_peer_classes[c];
	}

	peer_class const* peer_class_pool::at(peer_class_t c) const
	{
		if (c >= m_peer_classes.end_index() || !m_peer_classes[c].in_use) return nullptr;
		return &m_peer_classes[c];
	}
}
