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

logger = logging.getLogger('websockets')
logger.setLevel(logging.INFO)
logger.addHandler(logging.StreamHandler(sys.stdout))


async def handle(websocket, path):
    try:
        while True:
            message = await websocket.recv()

            print('{} - [{}] WS "{}..."'.format(
                websocket.remote_address[0],
                datetime.now().strftime("%d/%m/%Y %H:%M:%S"),
                message[:33]),
                file=sys.stderr)

            request = json.loads(message)
            response = {}
            response["info_hash"] = request["info_hash"]
            response["interval"] = 120
            response["min_interval"] = 60

            await websocket.send(json.dumps(response))

    except Exception as e:
        print(e)


if __name__ == '__main__':
    port = int(sys.argv[1])
    use_ssl = sys.argv[2] != '0'
    min_interval = sys.argv[3]
    print('python version: %s' % sys.version_info.__str__())

    if use_ssl:
        ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ssl_context.load_cert_chain('../ssl/server.pem')
    else:
        ssl_context = None

    start_server = websockets.serve(handle, '127.0.0.1', port, ssl=ssl_context)
    asyncio.get_event_loop().run_until_complete(start_server)
    asyncio.get_event_loop().run_forever()
