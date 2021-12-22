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
   See also https://certbot.eff.org/instructions.

3. Install the systemd development files:
   ```
   sudo apt install libsystemd-dev
   ```

2. A number of thirdparty repositories are used. To initialize them, run `git submodule update --init --recursive` in the project root.

3. For the websocket server part, uWebSockets are used (https://github.com/uNetworking/uWebSockets). The code should now exist in `thirdparty`.
   This library must be built with SSL-support once. From the projects root:
   ```
   cd thirdparty/uWebSockets/uSockets/ && make -j8 boringssl && cd ../../..
   cd thirdparty/uWebSockets/ && WITH_BORINGSSL=1 make -j8 && cd ../..
   ```

4. Build this project.
   ```
   mkdir build && cd build && cmake ..
   sudo make -j8 install
   ```

### Configuration
The resulting binary `znn-repro` has to be configured in a configuration file.
You can use the `create_configuration.py` script to generate one. Note that you have to run that under the account that you will use to
run `znn-repro`, i.e. if you want to use secure websockets, which require root privileges, run
```
python3 create_configuration.py
```
and input the required data. A `config.json` file will then be generated in `/home/<your-username>/.config/znn-repro` or `/root/.config/znn-repro`.

### Installing
If you run with root privileges, you can install the tool: `sudo make install`. `which znn-repro` should then print the path it installed to,
probably `/usr/local/bin`.

### Running
Execute `znn-repro` if you installed it, or `/path/to/znn-repro` else.

### Tests
A python script lives in directory `test`. Put that on a client computer and use it to test response times for two different scenarios:
1. Multiple clients connect concurrently, each sending a single request.
2. A single client connects and sends multiple requests concurrently.
