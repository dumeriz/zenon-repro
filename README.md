# Zenon reverse proxy

### Setup
1. This proxy supports both ws and wss; if wss is required, a valid certificate must exist.
   An option is a free certificate from https://letsencrypt.org. To acquire one, install certbot:
   ```
   sudo snap install core; sudo snap refresh core
   sudo snap install --classic certbot
   sudo ln -s /snap/bin/certbot /usr/bin/certbot
   # open http port so that certbot can communicate; can be closed afterwards
   sudo ufw allow http
   # execute certbot and follow the instructions to retrieve an ssl certificate
   ```

2. A number of thirdparty repositories are used. To initialize them, run `git submodule update --init --recursive` in the project root.

3. For the websocket server part, uWebSockets are used (https://github.com/uNetworking/uWebSockets). The code should now exist in `thirdparty`.
   This library must be built with SSL-support once. From the projects root:
   ```
   cd uWebSockets/uSockets/ && make -j8 boringssl && cd ../..
   cd uWebSockets/uSockets/ && WITH_BORINGSSL=1 make -j8 && cd ../..
   cd uWebSockets/ && WITH_BORINGSSL=1 make -j8 && cd ..
   ```

4. Build this project.
   ```
   mkdir build && cd build && cmake ..
   make -j8
   ```

### Running
The resulting binary `reverse_proxy` has several options. Just executing it without arguments prints a help text.
Examples:
- Start the listener on port 8085. Enable secure websockets by passing the directory containing the files `privkey.pem` and `fullchain.pem`:
  ```
  sudo ./reverse_proxy -p 8085 -c /etc/letsencrypt/live/<your-domain-name>
  ```
- Start the listener on port 8085. Use unsecure sockets and only a single listener thread. Specify a timeout of 20 milliseconds for calls to
  znnd, which listens on non-standard port 3333 for incoming websocket connections:
  ```
  ./reverse_proxy -p 8085 -z 3333 -n 1 -t 20
  ```

### Tests
A python script lives in directory `test`. Put that on a client computer and use it to test response times for two different scenarios:
1. Multiple clients connect concurrently, each sending a single request.
2. A single client connects and sends multiple requests concurrently.

