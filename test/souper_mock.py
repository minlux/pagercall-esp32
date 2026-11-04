#!/usr/bin/env python3
"""
Mock of the update server Souper, to test the update via the update server on the device.
Implements only the requests PagerCall makes (see include/souper.h), not the Souper UI.
Taken from Candela (esp8266/test/souper_mock.py).

   ./souper_mock.py --image ../.pio/build/heltec_wifi_lora_32_V3_test/firmware.signed.bin
   ./souper_mock.py --image firmware.signed.bin --dst firmware.dryrun
   ./souper_mock.py --image firmware.bin                                          (unsigned)
   ./souper_mock.py --image firmware.signed.bin --dst spiffs                      (-2 at once)
   ./souper_mock.py                                                               (no update, poll only)

The URL is fixed in the firmware. For the mock set SOUPER_URL of the variant
heltec_wifi_lora_32_V3_test in platformio.ini to "http://<ip-of-the-pc>:8090/souper", build it with
pio run -e heltec_wifi_lora_32_V3_test and install it. Use an image of this variant for --image, too
(otherwise the device asks the real server after the update).

On the device (any token, except "wrong": then 401):

   curl -X PUT -d '{"ota_token":"test"}' http://<device>/credentials
   curl -X POST http://<device>/ota/poll
   curl http://<device>/ota

Output: each request with the system info or the reported status. The update (id 17) stays open,
until status 2 or -2 is reported, as with Souper; "merker" is stored.

   --port 8090         port (base URL is http://<pc>:<port>/souper)
   --poll 300          interval in the poll response
   --slow 20000        deliver the image with 20000 bytes/s (e.g. to test a WLAN loss during the download)
"""
import argparse, hashlib, json, os, threading, time
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler

API = '/souper/assets/api'
UPDATE_ID = 17


class State:
    lock = threading.Lock()
    image = None        # bytes or None (no update)
    status = 0          # 0: open (pending), 2: success, -2: failed
    merker = 0
    args = None


def log(text):
    print(time.strftime('%H:%M:%S'), text, flush=True)


class Souper(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.0'

    def log_message(self, *a):
        pass

    def reply(self, code, body=b'', ctype='application/json'):
        b = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def token_ok(self):
        auth = self.headers.get('Authorization', '')
        if not auth.startswith('Bearer ') or auth[7:] in ('', 'wrong'):
            log('%s %s -> 401 (Authorization: %r)' % (self.command, self.path, auth[:20]))
            self.reply(401)
            return False
        return True

    def body(self):
        n = int(self.headers.get('Content-Length') or 0)
        data = self.rfile.read(n)
        try:
            return json.loads(data)
        except ValueError:
            log('  broken JSON: %r' % data[:200])
            return None

    def open_update(self):
        return State.image is not None and State.status == 0

    def do_PATCH(self):
        if not self.token_ok():
            return
        a = State.args
        if self.path.startswith(API + '/target/'):
            info = self.body()
            log('PATCH /target/%s  %s' % (self.path[len(API) + 8:], json.dumps(info, ensure_ascii=False)))
            with State.lock:
                updates = [UPDATE_ID] if self.open_update() else []
            return self.reply(200, json.dumps({'poll': a.poll, 'updates': updates}))
        if self.path == '%s/update/%d' % (API, UPDATE_ID):
            data = self.body() or {}
            log('PATCH /update/%d  %s' % (UPDATE_ID, json.dumps(data, ensure_ascii=False)))
            with State.lock:
                if State.status != 0:
                    return self.reply(202)  # already closed
                if data.get('status') in (2, -2):
                    State.status = data['status']
                if isinstance(data.get('merker'), int):
                    State.merker = data['merker']
            return self.reply(200)
        self.reply(404)

    def do_GET(self):
        a = State.args
        if self.path == '%s/update/%d' % (API, UPDATE_ID) and State.image is not None:
            if not self.token_ok():
                return
            with State.lock:
                image, merker, status = State.image, State.merker, State.status
            host = self.headers.get('Host', '127.0.0.1:%d' % a.port)
            md5 = hashlib.md5(image).hexdigest()  # as Souper; PagerCall does not check it
            update = {'id': UPDATE_ID, 'target': 1, 'file': {
                'id': 3, 'name': 'PagerCall', 'version': '9.999', 'size': len(image), 'type': '',
                'src': 'http://%s/souper/files/firmware.signed.bin' % host, 'dst': a.dst, 'md5': md5},
                'status': status, 'merker': merker, 'scheduled': int(time.time())}
            log('GET /update/%d  merker=%d dst=%s size=%d' % (UPDATE_ID, merker, a.dst, len(image)))
            return self.reply(200, json.dumps(update))
        if self.path == '/souper/files/firmware.signed.bin' and State.image is not None:
            log('GET %s  (%d bytes%s)' % (self.path, len(State.image), ', slow' if a.slow else ''))
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(len(State.image)))
            self.end_headers()
            try:
                if a.slow:
                    for i in range(0, len(State.image), 1000):
                        self.wfile.write(State.image[i:i + 1000])
                        time.sleep(1000 / a.slow)
                else:
                    self.wfile.write(State.image)
                log('  download complete')
            except OSError as e:
                log('  download aborted: %s' % e)
            return
        self.reply(404)


def main():
    p = argparse.ArgumentParser(description='Mock of Souper for PagerCall')
    p.add_argument('--port', type=int, default=8090)
    p.add_argument('--image', help='image offered as update 17 (without: no update)')
    p.add_argument('--dst', default='firmware')
    p.add_argument('--poll', type=int, default=300)
    p.add_argument('--slow', type=int, default=0, help='bytes/s of the download')
    State.args = a = p.parse_args()
    if a.image:
        State.image = open(a.image, 'rb').read()
    log('Souper mock on http://0.0.0.0:%d/souper, update: %s' %
        (a.port, '%s (%d bytes, dst=%s)' % (os.path.basename(a.image), len(State.image), a.dst) if a.image else 'none'))
    ThreadingHTTPServer(('0.0.0.0', a.port), Souper).serve_forever()


if __name__ == '__main__':
    main()
