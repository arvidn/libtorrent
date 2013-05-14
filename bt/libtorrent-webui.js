libtorrent_connection = function(url, callback)
{
	var self_ = this;
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

			if (!self_._transactions.hasOwnProperty(tid)) return;

			var callback = self_._transactions[tid];
			delete self_._transactions[tid];

			var ret = null;
			switch(fun)
			{
				case 0: // get_updates
					self_._frame = view.getUint32(4);
					var num_torrents = view.getUint32(8);
					console.log('frame: ' + self_._frame + ' num-torrents: ' + num_torrents);
					ret = {};
					var offset = 12;
					for (var i = 0; i < num_torrents; ++i)
					{
						var infohash = '';
						for (var j = 0; j < 20; ++j)
						{
							var b = view.getUint8(offset + j);
							infohash += b.toString(16);
						}
						offset += 20;
						var torrent = {};

						var mask_high = view.getUint32(offset);
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
									// TODO: factor this out
									var len = view.getUint16(offset);
									var name = '';
									offset += 2;
									for (var j = 0; j < len; ++j)
									{
										name += String.fromCharCode(view.getUint8(offset));
										offset += 1;
									}
									torrent['name'] = name;
									break;
								case 2: // total-uploaded
									break;

								case 8: // progress
									torrent['progress'] = view.getUint32(offset);
									offset += 4;
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
							}
						}
						// read fields, write into 'torrent'

						ret[infohash] = torrent;
					}
					break;

			};
			callback(ret);
		}

		console.log(ev);
	};
	this._socket.binaryType = "arraybuffer";
	this._frame = 0;
	this._transactions = {}

	this.get_updates = function(callback)
	{
		if (this._socket.readyState != WebSocket.OPEN)
		{
			window.setTimeout( function() { callback([]) }, 0);
			return;
		}
		
		var tid = Math.floor(Math.random() * 65535);
		this._transactions[tid] = callback;

		var call = new ArrayBuffer(15);
		var view = new DataView(call);
		// function 0
		view.setUint8(0, 0);
		// transaction-id
		view.setUint16(1, tid);
		// frame-number
		view.setUint32(3, this.frame);
		// bitmask (64 bits) [flags, name, progress, connected-peers, state]
		view.setUint32(7, 0);
		view.setUint32(11, (1 << 0) | (1 << 1) | (1 << 8) | (1 << 10) | (1 << 20));
		this._socket.send(call);
	}
}

