#!/usr/bin/env python

import asyncio
import websockets
import sys
import json
import random
import string
import time
import argparse
import multiprocessing

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
async def do_request(websocket, request, timeout, verbose=False):
    if verbose: print(f"Sending request {request}")
    await asyncio.wait_for(websocket.send(request), timeout=timeout)
    if verbose: print(f"Awaiting result")
    result = await asyncio.wait_for(websocket.recv(), timeout=timeout)
    if verbose: print(f"{result}")

# Executing a single client with concurrent requests
class concli:

    def __init__(self, endpoint, calls):
        self.address = endpoint
        self.requests = make_requests(calls)
    
    async def call(self, websocket, request):
        await do_request(websocket, request, 2, not silence)

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
            await do_request(websocket, request, 2, not silence)

    async def run(self):
        for r in self.requests:
            await self.call(r)

# Executing multiple clients from multiple processes, each encapsulating a concli
def run_client_process(client, verbose):
    global silence
    silence = not verbose
    try:
        asyncio.run(client.run())
    except TimeoutError:
        print("TIMEOUT")

class parcli:

    def __init__(self, endpoint, clients, calls):
        self.clients = [concli(endpoint, calls) for i in range(clients)]

    def run(self):
        processes = [multiprocessing.Process(target=run_client_process, args=(client, not silence)) for client in self.clients]

        for p in processes:
            p.start()
        for p in processes:
            p.join()


async def run(cli, host, amount):
    await cli(host, amount).run()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Exercise the reverse proxy with some stresstest')
    parser.add_argument('host', help='Address+port where the proxy is running (ex. wss://my.domain:443)')
    parser.add_argument('-m --mode', dest='mode', default='scc', choices=['mcc', 'scc', 'pcc'],
            help='"mcc": Concurrent clients; "scc": 1 client, concurrent requests; "pcc": multiple parallel clients with concurrent requests')
    parser.add_argument('-n --n', dest='amount', type=int, default=100, help='Amount of clients / requests')
    parser.add_argument('-r --requests', dest='requests', type=int, default=100, help='Amount of requests if mode is pcc')
    parser.add_argument('-v', '--verbose', dest='verbose', action='store_true', help='Output requests and responses if given')

    args = parser.parse_args(sys.argv[1:])

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

    if args.mode == "mcc":
        asyncio.run(run(multicli, args.host, args.amount))
    elif args.mode == "scc":
        asyncio.run(run(concli, args.host, args.amount))
    elif args.mode == "pcc":
        parcli(args.host, args.amount, args.requests).run()
        
    end = time.time()
    print(f"Execution finished in {end - start} seconds")

