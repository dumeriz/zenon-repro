import os
import json
import asyncio
import websockets
import subprocess

BOLD = '\33[1m'
RED = '\033[91m'
END = '\033[0m'
ERR = RED + BOLD

def is_root():
    return os.geteuid() == 0

def read_from_prompt(prompt, default):
    read = input(BOLD + prompt + f"Default: {default} > " + END).strip()
    if 0 == len(read):
        return default
    return read

def read_number(prompt, default, fallback):
    try:
        return int(read_from_prompt(prompt, str(default)))
    except ValueError:
        return fallback

def read_choice(prompt, default, fallback):
    read = read_from_prompt("y/Y or n/N: " + prompt, default)
    if read not in ['y', 'Y', 'n', 'N']:
        return fallback
    return read in ['y', 'Y']

def query_protocol():
    return read_choice("Use secure (wss) sockets? ", 'Y', False)

def query_timeout():
    return read_number("Timeout for the node connection in milliseconds (10-100). ", 25, 0)

def query_listen_port(ssl=False):
    return read_number("Public port (which clients connect to). ", 443 if ssl else 8001, 0)

def query_certificates_path():
    certpath = read_from_prompt("Directory containing privkey.pem and fullchain.pem. ", "/root/")
    if not os.path.exists(certpath):
        raise Exception(f"{certpath} is not readable (do you have the permissions?)")
    if not os.path.isfile(os.path.join(certpath, "privkey.pem")):
        raise Exception(f"privkey.pem does not exist in {certpath}")
    if not os.path.isfile(os.path.join(certpath, "fullchain.pem")):
        raise Exception(f"fullchain.pem does not exist in {certpath}")

def query_zenon_host():
    return read_from_prompt("Zenon Node URL. ", "ws://localhost:35998")

async def validate_node(address):
    print(f"  -- Checking availability of {address} ...")
    request = json.dumps({"jsonrpc": "2.0", "id": 2, "method": "embedded.pillar.getQsrRegistrationCost", "params": []})
    try:
        async with websockets.connect(address) as websocket:
            await asyncio.wait_for(websocket.send(request), timeout=2)
            result = await asyncio.wait_for(websocket.recv(), timeout=2)
            if 'error' in result:
                raise Exception(f"Received error response {result['error']}")
    except Exception as e:
        raise Exception(f"{address} did not reply to a Zenon-API request: {e}")
    print(f"  -- Ok")
    return address

def open_firewall(port):
    pass

def query_proxies():
    print("""
Define the list of reverse proxies you want to start.
- Node Address: Websocket connection the Zenon Node is reachable at (wss or ws, including port).
- Secure Sockets: Should the proxy accept connections via wss or ws?
- Port: port this proxy will listen on for client connections.

Several proxies may listen on the same port and also may refer to the same Zenon Node.""")

    def make_proxy_entry():
        node = asyncio.run(validate_node(query_zenon_host()))
        wss = query_protocol()
        return {"node": node, "wss": wss, "port": query_listen_port(wss), 'timeout': query_timeout()}

    proxies = [make_proxy_entry()]
    while read_choice("Configure another proxy? ", "Y", False):
        proxies.append(make_proxy_entry())

    if len(proxies) == 0:
        raise Exception("No proxies defined")
    return proxies

def get_config():
    config = {'proxies': query_proxies(), 'certificates': ""}
    print(config['proxies'])
    if any([proxy['wss'] for proxy in config['proxies']]):
        config['certificates'] = query_certificates_path()
    return config

def try_open_firewall(ports):
    if not is_root():
        print(f"{ERR}Not root; Can't open the firewall. Check manually that these ports are open: {ports}{END}")
        return
    print(f"Opening the firewall for ports {ports}")
    for port in ports:
        result = subprocess.call(f"ufw allow {port}", shell=True)
        if result != 0:
            raise Exception("Opening the firewall failed")

def make_get_config_folder():
    path = os.path.expanduser('~/.config/znn-repro/')
    if os.path.isdir(path):
        return path

    try:
        os.makedirs(path)
    except Exception as e:
        print(f"Could not create configuration folder '{path}'")
        exit(-1)
    return path

if __name__ == '__main__':
    try:
        config = get_config()
        try_open_firewall([proxy['port'] for proxy in config['proxies']])

        js = json.dumps(config, indent=4)
        dir = make_get_config_folder()
        file = os.path.join(dir, "config.json")

        print(f"\nWriting to {file}:\n{js}")

        with open(file, 'w') as out:
            out.write(js)
            out.write('\n')

    except Exception as e:
        print(ERR + "Aborting: " + str(e) + END)
        exit(-1)

