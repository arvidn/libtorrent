
// read a string with a 16 bit length prefix
function read_string16(view, offset)
{
	var len = view.getUint16(offset);
	var str = '';
	offset += 2;
	for (var j = 0; j < len; ++j)
	{
		str += String.fromCharCode(view.getUint8(offset));
		++offset;
	}
	return str;
}
function read_string8(view, offset)
{
	var len = view.getUint8(offset);
	var str = '';
	++offset;
	for (var j = 0; j < len; ++j)
	{
		str += String.fromCharCode(view.getUint8(offset));
		++offset;
	}
	return str;
}

// read a 64 bit value
function read_uint64(view, offset)
{
	var high = view.getUint32(offset);
	offset += 4;
	var low = view.getUint32(offset);
	offset += 4;
	return high * 4294967295 + low;
}

function _check_error(e, callback)
{
	if (e == 0) return false;

	var error = 'unknown error';
	switch (e)
	{
		case 1: error = 'no such function'; break;
		case 2: error = 'invalid number of arguments'; break;
		case 3: error = 'invalid argument type'; break;
		case 4: error = 'invalid argument'; break;
		case 5: error = 'truncated message'; break;
	}
	
	if (typeof(callback) !== 'undefined') callback(error);
}

libtorrent_connection = function(url, callback)
{
	var self = this;

	this._socket = new WebSocket(url);
	this._socket.onopen = function(ev) { callback("OK"); };
	this._socket.onerror = function(ev) { callback(ev.data); };
	this._socket.onmessage = function(ev)
	{
		var view = new DataView(ev.data);
		var fun = view.getUint8(0);
		var tid = view.getUint16(1);

		if (fun >= 128)
		{
			var e = view.getUint8(3);
			fun &= 0x7f;
			console.log('RESPONSE: fun: ' + fun + ' tid: ' + tid + ' error: ' + e);

			if (!self._transactions.hasOwnProperty(tid)) return;

			var handler = self._transactions[tid];
			delete self._transactions[tid];

			// this handler will deal with parsing out the remaining
			// return value and pass it on to the user supplied
			// callback function
			handler(view, fun, e);
		}
		else
		{
			// This is a function call

		}

	};
	this._socket.binaryType = "arraybuffer";
	this._frame = 0;
	this._transactions = {}
	this._tid = 0;
}

libtorrent_connection.prototype['list_settings'] = function(callback)
{
	if (this._socket.readyState != WebSocket.OPEN)
	{
		window.setTimeout( function() { callback("socket closed"); }, 0);
		return;
	}
	
	var tid = this._tid++;
	if (this._tid > 65535) this._tid = 0;

	// this is the handler of the response for this call. It first
	// parses out the return value, the passes it on to the user
	// supplied callback.
	var self = this;
	this._transactions[tid] = function(view, fun, e)
	{
		if (_check_error(e, callback)) return;
		var ret = [];
		var num_strings = view.getUint32(4);
		var num_ints = view.getUint32(8);
		var num_bools = view.getUint32(12);

		// this is a local copy of the settings-id -> type map
		// the types are encoded as 0 = string, 1 = int, 2 = bool.
		self._settings = {};

		var offset = 16;
		for (var i = 0; i < num_strings + num_ints + num_bools; ++i)
		{
			var name = read_string8(view, offset);
			offset += 1 + name.length;
			var code = view.getUint16(offset);
			offset += 2;
			var type;
			if (i >= num_strings + num_ints)
			{
				type = 'bool';
				self._settings[code] = 2;
			}
			else if (i >= num_strings)
			{
				type = 'int';
				self._settings[code] = 1;
			}
			else
			{
				type = 'string';
				self._settings[code] = 0;
			}
			
			ret.push({'name': name, 'id': code, 'type': type});
		}

		if (typeof(callback) !== 'undefined') callback(ret);
	};

	var call = new ArrayBuffer(3);
	var view = new DataView(call);
	// function 14
	view.setUint8(0, 14);
	// transaction-id
	view.setUint16(1, tid);

	console.log('CALL list_settings() tid = ' + tid);
	this._socket.send(call);
}

libtorrent_connection.prototype['get_settings'] = function(settings, callback)
{
	// TODO: factor out this RPC boiler plate
	if (this._socket.readyState != WebSocket.OPEN)
	{
		window.setTimeout( function() { callback("socket closed"); }, 0);
		return;
	}

	if (typeof(this._settings) === 'undefined')
	{
		window.setTimeout( function() { callback("must call list_settings first"); }, 0);
		return;
	}

	var tid = this._tid++;
	if (this._tid > 65535) this._tid = 0;

	// this is the handler of the response for this call. It first
	// parses out the return value, the passes it on to the user
	// supplied callback.
	var self = this;
	this._transactions[tid] = function(view, fun, e)
	{
		if (_check_error(e, callback)) return;
		var num_settings = view.getUint16(4);
		var offset = 6;

		var ret = [];
		
		if (settings.length != num_settings)
		{
			callback("get_settings returned invalid number of items");
			return;
		}
		for (var i = 0; i < num_settings; ++i)
		{
			var type = self._settings[settings[i]];
			if (typeof(type) !== 'number' || type < 0 || type > 2)
			{
				if (typeof(callback) !== 'undefined') callback("invalid setting ID (" + settings[i] + ")");
				return;
			}
			switch (type)
			{
				case 0: // string
					var n = read_string16(view, offset);
					ret.push(n);
					offset += 2 + n.length;
					break;
				case 1: // int
					ret.push(view.getUint32(offset));
					offset += 4;
					break;
				case 2: // bool
					ret.push(view.getUint8(offset) ? true : false);
					offset += 1;
					break;
			};
		}
		if (typeof(callback) !== 'undefined') callback(ret);
	}

	var call = new ArrayBuffer(5 + settings.length * 2);
	var view = new DataView(call);
	// function 16
	view.setUint8(0, 16);
	// transaction-id
	view.setUint16(1, tid);
	// num settings
	view.setUint16(3, settings.length);

	var offset = 5;
	for (var i = 0; i < settings.length; ++i)
	{
		view.setUint16(offset, settings[i]);
		offset += 2;
	}

	console.log('CALL get_settings( num: ' + settings.length + ' ) tid = ' + tid);
	this._socket.send(call);
}

libtorrent_connection.prototype['get_updates'] = function(mask, callback)
{
	if (this._socket.readyState != WebSocket.OPEN)
	{
		window.setTimeout( function() { callback("socket closed"); }, 0);
		return;
	}
	
	var tid = this._tid++;
	if (this._tid > 65535) this._tid = 0;

	// this is the handler of the response for this call. It first
	// parses out the return value, the passes it on to the user
	// supplied callback.
	var self = this;
	this._transactions[tid] = function(view, fun, e)
	{
		if (_check_error(e, callback)) return;

		self._frame = view.getUint32(4);
		var num_torrents = view.getUint32(8);
		console.log('frame: ' + self._frame + ' num-torrents: ' + num_torrents);
		ret = {};
		var offset = 12;
		for (var i = 0; i < num_torrents; ++i)
		{
			var infohash = '';
			for (var j = 0; j < 20; ++j)
			{
				var b = view.getUint8(offset + j);
				if (b < 16) infohash += '0';
				infohash += b.toString(16);
			}
			offset += 20;
			var torrent = {};

//			var mask_high = view.getUint32(offset);
			offset += 4;
			var mask_low = view.getUint32(offset);
			offset += 4;

			for (var field = 0; field < 32; ++field)
			{
				var mask = 1 << field;
				if ((mask_low & mask) == 0) continue;
				switch (field)
				{
					case 0: // flags
						// skip high bytes, since we can't
						// represent 64 bits in one field anyway
						offset += 4;
						torrent['flags'] = view.getUint32(offset);
						offset += 4;
						break;
					case 1: // name
						var name = read_string16(view, offset);
						offset += 2 + name.length;
						torrent['name'] = name;
						break;
					case 2: // total-uploaded
						torrent['total-uploaded'] = read_uint64(view, offset);
						offset += 8;
						break;
					case 3: // total-downloaded
						torrent['total-downloaded'] = read_uint64(view, offset);
						offset += 8;
						break;
					case 4: // added-time
						torrent['added-time'] = read_uint64(view, offset);
						offset += 8;
						break;
					case 5: // completed-time
						torrent['completed-time'] = read_uint64(view, offset);
						offset += 8;
						break;
					case 6: // upload-rate
						torrent['upload-rate'] = view.getUint32(offset);
						offset += 4;
						break;
					case 7: // download-rate
						torrent['download-rate'] = view.getUint32(offset);
						offset += 4;
						break;
					case 8: // progress
						torrent['progress'] = view.getUint32(offset);
						offset += 4;
						break;
					case 9: // error
						var e = read_string16(view, offset);
						offset += 2 + e.length;
						torrent['error'] = e;
						break;
					case 10: // connected-peers
						torrent['connected-peers'] = view.getUint32(offset);
						offset += 4;
						break;
					case 11: // connected-seeds
						torrent['connected-seeds'] = view.getUint32(offset);
						offset += 4;
						break;
					case 12: // downloaded-pieces
						torrent['downloaded-pieces'] = view.getUint32(offset);
						offset += 4;
						break;
					case 13: // total-done
						torrent['total-done'] = read_uint64(view, offset);
						offset += 8;
						break;
					case 14: // distributed-copies
						var integer = view.getUint32(offset);
						offset += 4;
						var fraction = view.getUint32(offset);
						offset += 4;
						torrent['distributed-copies'] = integer + (fraction / 1000.0);
						break;
					case 15: // all-time-upload
						torrent['all-time-upload'] = read_uint64(view, offset);
						offset += 8;
						break;
					case 16: // all-time-download
						torrent['all-time-download'] = read_uint64(view, offset);
						offset += 8;
						break;
					case 17: // unchoked-peers
						torrent['unchoked-peers'] = view.getUint32(offset);
						offset += 4;
						break;
					case 18: // num-connections
						torrent['num-connections'] = view.getUint32(offset);
						offset += 4;
						break;
					case 19: // queue-position
						torrent['queue-position'] = view.getUint32(offset);
						offset += 4;
						break;
					case 20: // state
						torrent['state'] = view.getUint8(offset);
						offset += 1;
						break;
					case 21: // failed-bytes
						torrent['failed-bytes'] = read_uint64(view, offset);
						offset += 8;
						break;
					case 22: // redundant-bytes
						torrent['redundant-bytes'] = read_uint64(view, offset);
						offset += 8;
						break;
				}
			}
			ret[infohash] = torrent;
		}
		if (typeof(callback) !== 'undefined') callback(ret);
	};

	var call = new ArrayBuffer(15);
	var view = new DataView(call);
	// function 0
	view.setUint8(0, 0);
	// transaction-id
	view.setUint16(1, tid);
	// frame-number
	view.setUint32(3, this._frame);
	view.setUint32(7, 0);
	view.setUint32(11, mask);

	console.log('CALL get_updates( frame: ' + this._frame + ' mask: ' + mask.toString(16) + ' ) tid = ' + tid);
	this._socket.send(call);
}

libtorrent_connection.prototype['start'] = function(info_hashes, callback)
{ this._send_simple_call(1, info_hashes, callback); };

libtorrent_connection.prototype['stop'] = function(info_hashes, callback)
{ this._send_simple_call(2, info_hashes, callback); };

libtorrent_connection.prototype['set_auto_managed'] = function(info_hashes, callback)
{ this._send_simple_call(3, info_hashes, callback); };

libtorrent_connection.prototype['clear_auto_managed'] = function(info_hashes, callback)
{ this._send_simple_call(4, info_hashes, callback); };

libtorrent_connection.prototype['queue_up'] = function(info_hashes, callback)
{ this._send_simple_call(5, info_hashes, callback); };

libtorrent_connection.prototype['queue_down'] = function(info_hashes, callback)
{ this._send_simple_call(6, info_hashes, callback); };

libtorrent_connection.prototype['queue_top'] = function(info_hashes, callback)
{ this._send_simple_call(7, info_hashes, callback); };

libtorrent_connection.prototype['queue_bottom'] = function(info_hashes, callback)
{ this._send_simple_call(8, info_hashes, callback); };

libtorrent_connection.prototype['remove'] = function(info_hashes, callback)
{ this._send_simple_call(9, info_hashes, callback); };

libtorrent_connection.prototype['remove_with_data'] = function(info_hashes, callback)
{ this._send_simple_call(10, info_hashes, callback); };

libtorrent_connection.prototype['force_recheck'] = function(info_hashes, callback)
{ this._send_simple_call(11, info_hashes, callback); };

libtorrent_connection.prototype['set_sequential_download'] = function(info_hashes, callback)
{ this._send_simple_call(12, info_hashes, callback); };

libtorrent_connection.prototype['clear_sequential_download'] = function(info_hashes, callback)
{ this._send_simple_call(13, info_hashes, callback); };

libtorrent_connection.prototype._send_simple_call = function(fun_id, info_hashes, callback)
{
	var call = new ArrayBuffer(3 + 2 + info_hashes.length * 20);
	var view = new DataView(call);

	if (fun_id < 1 || fun_id > 13)
	{
		window.setTimeout( function() { callback("socket closed"); }, 0);
		return;
	}

	var tid = this._tid++;
	if (this._tid > 65535) this._tid = 0;

	// function-id
	view.setUint8(0, fun_id);
	// transaction-id
	view.setUint16(1, tid);
	// num_torrents
	view.setUint16(3, info_hashes.length);

	var offset = 5;
	for (ih in info_hashes)
	{
		for (var i = 0; i < 40; i += 2)
		{
			var b = parseInt(info_hashes[ih].substring(i, i + 2), 16);
			view.setUint8(offset, b);
			offset += 1;
		}
	}

	console.log('CALL ' + fun_id + '() tid = ' + tid);

	// this is the handler of the response for this call. It first
	// parses out the return value, the passes it on to the user
	// supplied callback.
	this._transactions[tid] = function(view, fun, e)
	{
		if (_check_error(e, callback)) return;
		var num_torrents = view.getUint16(4);
		if (typeof(callback) !== 'undefined') callback(num_torrents);
	};

	this._socket.send(call);
}

fields =
{
	'flags': 1 << 0,
	'name': 1 << 1,
	'total_uploaded': 1 << 2,
	'total_downloaded': 1 << 3,
	'added_time': 1 << 4,
	'completed_time': 1 << 5,
	'upload_rate': 1 << 6,
	'download_rate': 1 << 7,
	'progress': 1 << 8,
	'error': 1 << 9,
	'connected_peers': 1 << 10,
	'connected_seeds': 1 << 11,
	'downloaded_pieces': 1 << 12,
	'total_done': 1 << 13,
	'distributed_copies': 1 << 14,
	'all_time_upload': 1 << 15,
	'all_time_download': 1 << 16,
	'unchoked_peers': 1 << 17,
	'num_connections': 1 << 18,
	'queue_position': 1 << 19,
	'state': 1 << 20,
	'failed_bytes': 1 << 21,
	'redundant_bytes': 1 << 22
};

// prevent the compiler from optimizing these away
window['libtorrent_connection'] = libtorrent_connection;
window['fields'] = fields;

