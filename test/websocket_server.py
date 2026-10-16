#!/usr/bin/env python3

import sys
import ssl
import json
import asyncio
import logging
from datetime import datetime

# Remove '.' from sys.path or we try to import the http.py module
sys.path = sys.path[1:]

import websockets
from websockets.legacy.server import serve

class IgnorePongOnceProtocol(websockets.WebSocketServerProtocol):
    connection_count = 0

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.suppress_pongs = type(self).connection_count == 0
        type(self).connection_count += 1

    async def pong(self, data=b''):
        if self.suppress_pongs:
            # Keep the first connection open but stop it proving liveness.
            return
        await super().pong(data)

logger = logging.getLogger('websockets')
logger.setLevel(logging.INFO)
logger.addHandler(logging.StreamHandler(sys.stdout))


async def handle(websocket):
    # the tracker behavior to simulate:
    # normal: respond to every announce
    # failure: respond to every announce with a failure reason
    # stale-failure: respond normally, then send a failure reason that isn't
    #   the outcome of the announce (like aquatic does when an answer arrives
    #   for an offer it no longer knows about)
    # bare-failure: inject a failure response without an info_hash, then send
    #   a normal, correlated response on the same connection. Tests that the
    #   client ignores the uncorrelated failure without closing the connection.
    # reconnect: suppress pongs on the first connection, respond to its initial
    #   hybrid-torrent announces, then ignore further announces on that socket.
    #   A replacement connection responds normally.
    mode = sys.argv[3] if len(sys.argv) > 3 else 'normal'
    announce_count = 0
    stale_connection = mode == 'reconnect' and getattr(websocket, 'suppress_pongs', False)

    try:
        while True:
            message = await websocket.recv()

            print('{} - [{}] WS "{}..."'.format(
                websocket.remote_address[0],
                datetime.now().strftime("%d/%m/%Y %H:%M:%S"),
                message[:33]),
                file=sys.stderr)

            request = json.loads(message)
            info_hash = request["info_hash"]
            announce_count += 1

            # The test torrent is hybrid and sends one initial announce for
            # each info hash. Let those complete, then stop responding on the
            # first connection. Only a fresh connection will receive a reply.
            if stale_connection and announce_count > 2:
                continue

            if mode == 'failure':
                await websocket.send(json.dumps({
                    "failure reason": "test failure",
                    "info_hash": info_hash}))
                continue

            if mode == 'bare-failure':
                await websocket.send(json.dumps({
                    "failure reason": "Invalid request"}))

            # Delay one response on the replacement connection to test that
            # it stays alive across multiple ping/pong intervals.
            if mode == 'reconnect' and not stale_connection and announce_count == 2:
                await asyncio.sleep(22)

            response = {}
            response["info_hash"] = info_hash
            response["interval"] = 120
            response["min_interval"] = 60

            await websocket.send(json.dumps(response))

            if mode == 'stale-failure':
                await websocket.send(json.dumps({
                    "failure reason": "Could not find the offer corresponding"
                    " to your answer. It may have expired.",
                    "info_hash": info_hash}))

    except Exception as e:
        print(e)


async def main() -> None:
    use_ssl = sys.argv[1] != '0'
    print('python version: %s' % sys.version_info.__str__())

    if use_ssl:
        ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ssl_context.load_cert_chain('../ssl/server.pem')
    else:
        ssl_context = None

    mode = sys.argv[3] if len(sys.argv) > 3 else 'normal'
    kwargs = {}
    if mode == 'reconnect':
        kwargs['create_protocol'] = IgnorePongOnceProtocol

    server = await serve(handle, '127.0.0.1', 0, ssl=ssl_context, **kwargs)
    port = server.sockets[0].getsockname()[1]
    print(f'LISTENING_PORT {port}')
    sys.stdout.flush()

if __name__ == '__main__':
    loop = asyncio.new_event_loop()
    loop.run_until_complete(main())
    loop.run_forever()
