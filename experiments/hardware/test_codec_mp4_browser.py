"""Chrome playback/seek of the owned codec regression outputs, including mdhd v1."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import threading
from playwright.sync_api import sync_playwright


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    root = args.directory.resolve()
    class QuietHandler(SimpleHTTPRequestHandler):
        def log_message(self, *_):
            pass
        def send_head(self):
            name = self.path.split('?', 1)[0].lstrip('/')
            if name not in ('0.mp4', '1.mp4', '2.mp4'):
                self.send_error(404)
                return None
            file = (root / name).open('rb')
            size = (root / name).stat().st_size
            first, last = 0, size - 1
            partial = self.headers.get('Range')
            if partial:
                match = re.fullmatch(r'bytes=(\d+)-(\d*)', partial)
                if not match:
                    file.close()
                    self.send_error(416)
                    return None
                first = int(match[1])
                last = min(last, int(match[2])) if match[2] else last
            self.send_response(206 if partial else 200)
            self.send_header('Content-Type', 'video/mp4')
            self.send_header('Accept-Ranges', 'bytes')
            self.send_header('Content-Length', str(last - first + 1))
            if partial:
                self.send_header('Content-Range', f'bytes {first}-{last}/{size}')
            self.end_headers()
            file.seek(first)
            self.remaining = last - first + 1
            return file
        def copyfile(self, source, output):
            while self.remaining:
                chunk = source.read(min(self.remaining, 65536))
                if not chunk:
                    break
                output.write(chunk)
                self.remaining -= len(chunk)
    server = ThreadingHTTPServer(('127.0.0.1', 0), partial(QuietHandler, directory=str(root)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    report = {'passed': False, 'clips': []}
    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True, executable_path='C:/Program Files/Google/Chrome/Application/chrome.exe',
                                        args=['--no-proxy-server', '--autoplay-policy=no-user-gesture-required'])
            page = browser.new_page()
            for number in range(3):
                page.goto('http://127.0.0.1:' + str(server.server_port) + '/' + str(number) + '.mp4', wait_until='domcontentloaded')
                video = page.locator('video')
                video.wait_for()
                result = video.evaluate('''async v => {
                  v.muted = true; await v.play(); await new Promise(r => setTimeout(r, 1200));
                  const playing = v.currentTime; v.pause();
                  const seeking = new Promise((resolve, reject) => {
                    v.addEventListener('seeked', resolve, {once:true});
                    setTimeout(() => reject(new Error('seek timeout')), 10000);
                  });
                  v.currentTime = Math.max(0.5, v.duration - 0.5); await seeking;
                  return {width:v.videoWidth, height:v.videoHeight, playing, duration:v.duration,
                    afterSeek:v.currentTime, error:v.error && v.error.code};
                }''')
                report['clips'].append(result)
                print('BROWSER', number, json.dumps(result), flush=True)
                assert result['width'] == (320 if number == 1 else 640)
                assert result['playing'] > 0 and result['error'] is None
                assert result['afterSeek'] >= result['duration'] - 1
            browser.close()
        report['passed'] = True
        print(json.dumps(report), flush=True)
    finally:
        server.shutdown()
        server.server_close()
        (root / 'browser.json').write_text(json.dumps(report, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
