/*

Copyright (c) 2013, Arvid Norberg
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

#ifndef TORRENT_PERM_INTERFACE_HPP
#define TORRENT_PERM_INTERFACE_HPP

#include <string>

namespace libtorrent
{

	/**
		This is the interface an object need to implement in order
		to specify custom access permissions.
	*/
	struct permissions_interface
	{
		/// If returning true, the user may start torrents
		virtual bool allow_start() const = 0;

		/// If returning true, the user may stop torrents
		virtual bool allow_stop() const = 0;

		/// If returning true, the user may re-check torrents
		virtual bool allow_recheck() const = 0;

		/// If returning true, the user may modifiy the priority of files
		virtual bool allow_set_file_prio() const = 0;

		/// If returning true, the user may list torrents
		virtual bool allow_list() const = 0;

		/// If returning true, the user may add torrents
		virtual bool allow_add() const = 0;

		/// If returning true, the user may remove torrents
		virtual bool allow_remove() const = 0;

		/// If returning true, the user may remove torrents
		/// and delete their data from disk
		virtual bool allow_remove_data() const = 0;

		/// If returning true, the user may queue-up or -down torrents torrents
		virtual bool allow_queue_change() const = 0;

		/// If returning true, the user may GET the specified setting
		/// \param name is the constant used in libtorrent::settings_pack
		/// or -1 for settings that don't fit a libtorrent setting
		virtual bool allow_get_settings(int name) const = 0;

		/// If returning true, the user may SET the specified setting
		/// \param name is the constant used in libtorrent::settings_pack
		/// or -1 for settings that don't fit a libtorrent setting
		virtual bool allow_set_settings(int name) const = 0;

		/// If returning true, the user may download the content of torrents
		virtual bool allow_get_data() const = 0;

		// TODO: separate permissions to alter torrent state. separate different categories of settings?
		/// If returning true, the user is allowed to query session status,
		/// like global upload and download rates
		virtual bool allow_session_status() const = 0;
	};

	/**
		The interface to an authentication module. This plugs into web interfaces
		to authenticate users and determine their access permissions. The two main
		implementations are auth and pam_auth.
	*/
	struct auth_interface
	{
		/// finds an appropriate permissions objects for the given account.
		/// \return the persmissions object for the specified
		/// account, or NULL in case authentication fails.
		virtual permissions_interface const* find_user(std::string username, std::string password) const = 0;
	};

	/// an implementation of permissions_interface that reject all access
	struct no_permissions : permissions_interface
	{
		no_permissions() {}
		bool allow_start() const { return false; }
		bool allow_stop() const { return false; }
		bool allow_recheck() const { return false; }
		bool allow_set_file_prio() const { return false; }
		bool allow_list() const { return false; }
		bool allow_add() const { return false; }
		bool allow_remove() const { return false; }
		bool allow_remove_data() const { return false; }
		bool allow_queue_change() const { return false; }
		bool allow_get_settings(int) const { return false; }
		bool allow_set_settings(int) const { return false; }
		bool allow_get_data() const { return false; }
		bool allow_session_status() const { return false; }
	};

	/// an implementation of permissions_interface that only allow inspecting
	/// the stat of the torrent client, not altering it in any way. No modification
	/// of settings, no adding/removing/rechecking of torrents.
	struct read_only_permissions : permissions_interface
	{
		read_only_permissions() {}
		bool allow_start() const { return false; }
		bool allow_stop() const { return false; }
		bool allow_recheck() const { return false; }
		bool allow_set_file_prio() const { return false; }
		bool allow_list() const { return true; }
		bool allow_add() const { return false; }
		bool allow_remove() const { return false; }
		bool allow_remove_data() const { return false; }
		bool allow_queue_change() const { return false; }
		bool allow_get_settings(int) const { return true; }
		bool allow_set_settings(int) const { return false; }
		bool allow_get_data() const { return true; }
		bool allow_session_status() const { return true; }
	};

	/// an implementation of permissions_interface that permit all access.
	struct full_permissions : permissions_interface
	{
		full_permissions() {}
		bool allow_start() const { return true; }
		bool allow_stop() const { return true; }
		bool allow_recheck() const { return true; }
		bool allow_set_file_prio() const { return true; }
		bool allow_list() const { return true; }
		bool allow_add() const { return true; }
		bool allow_remove() const { return true; }
		bool allow_remove_data() const { return true; }
		bool allow_queue_change() const { return true; }
		bool allow_get_settings(int) const { return true; }
		bool allow_set_settings(int) const { return true; }
		bool allow_get_data() const { return true; }
		bool allow_session_status() const { return true; }
	};

}

#endif // TORRENT_PERM_INTERFACE_HPP

