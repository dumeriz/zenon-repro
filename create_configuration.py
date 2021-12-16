import os
import json

def is_root():
    return os.geteuid() == 0

def validate(config):
    if config['wss']:
        if not is_root():
            raise Exception("Secure sockets selected but current user is not root")

        keypath = os.path.join(config['certificates_path'], 'privkey.pem')
        chainpath = os.path.join(config['certificates_path'], 'fullchain.pem')
        if not (os.path.isfile(keypath) and os.path.isfile(chainpath)):
            raise Exception(f"'fullchain.pem' and/or 'privkey.pem' not found in '{config['certificates_path']}'")

    if config['threads'] < 1 or 20 < config['threads']:
        raise Exception(f"1-20 listener threads should be used, not '{config['threads']}'")

    if config['znn_port'] == 0 or config['listen_port'] == 0:
        raise Exception(f"Invalid value for Zenon Node WS-Port or the public port")

    return config

def read_from_prompt(prompt, default):
    read = input(prompt + f"Default: {default} > ").strip()
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

def query_threads():
    return read_number("Number of parallel listener threads (1-20). ", 10, 0)

def query_protocol():
    return read_choice("Use secure (wss) sockets? ", 'Y', False)

def query_timeout():
    return read_number("Timeout for the zenon-node-connection in milliseconds (10-100). ", 25, 0)

def query_listen_port(ssl=False):
    return read_number("Public port to listen on. ", 443 if ssl else 8001, 0)

def query_zenon_port():
    return read_number("Websocket port the Zenon Node is listening on. ", 35998, 0)

def query_certificates_path():
    return read_from_prompt("Directory containing privkey.pem and fullchain.pem. ", "/root/")

def get_config():
    config = {'threads': query_threads(),
            'wss': query_protocol(),
            'timeout': query_timeout(),
            'znn_port': query_zenon_port()}

    config['listen_port'] = query_listen_port(ssl=config['wss'])

    if config['wss']:
        config['certificates_path'] = query_certificates_path()

    return config

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
        js = json.dumps(validate(get_config()), indent=4)
        dir = make_get_config_folder()
        file = os.path.join(dir, "config.json")

        print(f"\nWriting to {file}:\n{js}")

        with open(file, 'w') as out:
            out.write(js)
            out.write('\n')

    except Exception as e:
        print("Aborting: " + str(e))
        exit(-1)

