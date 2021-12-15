#!/usr/bin/env python

import asyncio
import websockets
import sys
import json
import random
import string
import time
import argparse

global silence

def random_string(length):
   return ''.join(random.choices(string.ascii_letters + string.digits, k=length))

available_methods = [
	["embedded.pillar.getQsrRegistrationCost", []],
	["embedded.pillar.checkNameAvailability", [random_string(10)]],
    ["embedded.pillar.getAll", [0, 1]]]

def make_methods(num):
    methods = []
    while len(methods) < num:
        methods += available_methods
    return methods[:num]

def make_requests(num):
    return [json.dumps({"jsonrpc": "2.0", "id": 2, "method": m[0], "params": m[1]})
            for m in make_methods(num)]
	
# Preventing hangups with wait_for, in case the server is currently testing low timeout limits;
# not catching any errors though so timeouts will kill the process.
async def do_request(websocket, request, timeout):
    if not silence: print(f"Sending request {request}")
    await asyncio.wait_for(websocket.send(request), timeout=timeout)
    if not silence: print(f"Awaiting result")
    result = await asyncio.wait_for(websocket.recv(), timeout=timeout)
    if not silence: print(f"{result}")

# Executing a single client with concurrent requests
class parcli:

    def __init__(self, endpoint, calls):
        self.address = endpoint
        self.requests = make_requests(calls)
    
    async def call(self, websocket, request):
        await do_request(websocket, request, 2)

    async def run(self):
        async with websockets.connect(self.address) as websocket:
            for r in self.requests:
                await self.call(websocket, r)

# Executing multiple clients, each with a single request
class multicli:

    def __init__(self, endpoint, calls):
        self.address = endpoint
        self.requests = make_requests(calls)
    
    async def call(self, request):
        async with websockets.connect(self.address) as websocket:
            await do_request(websocket, request, 2)

    async def run(self):
        for r in self.requests:
            await self.call(r)

async def run(cli, host, amount):
    await cli(host, amount).run()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Exercise the reverse proxy with some stresstest')
    parser.add_argument('host', help='Address+port where the proxy is running (ex. wss://my.domain:443)')
    parser.add_argument('-m --mode', dest='mode', default='par', choices=['mcl', 'par'], help='"mcl": Multiple clients; "par": 1 client, concurrent requests')
    parser.add_argument('-n --n', dest='amount', type=int, default=100, help='Amount of requests for par or seq modes')
    parser.add_argument('-v', '--verbose', dest='verbose', action='store_true', help='Output requests and responses if given')

    args = parser.parse_args(sys.argv[1:])
    print(f"{args.host}, {args.mode}, {args.amount}, {args.verbose}")

    host_parts = args.host.split(':')
    try:
        if host_parts[0] not in ["ws", "wss"] or not host_parts[1].startswith('//'):
            raise Exception("")
        port = int(host_parts[2])
    except Exception:
        print("Hostformat: protocol://domain:port e.g. http://my.domain.com:8080. Proto may be ws or wss.")
        exit(-1)

    global silence
    silence = not args.verbose

    start = time.time()

    if args.mode == "mcl":
        asyncio.run(run(multicli, args.host, args.amount))
    elif args.mode == "par":
        asyncio.run(run(parcli, args.host, args.amount))
        
    end = time.time()
    print(f"Execution finished in {end - start} seconds")

