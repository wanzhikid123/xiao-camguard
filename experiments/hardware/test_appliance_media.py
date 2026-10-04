"""Authenticated photo/media gate; deletes only photos created by this run.

Uses the existing product account from its ignored local password file.
Saves private JPEG/AVI evidence, preserves all pre-existing media, exercises
paging/path checks and revocation of a deliberately slow owned-video download.
"""
import argparse
import http.client
import json
import re
from pathlib import Path
import socket
import threading
import time
from urllib.error import HTTPError
from urllib.parse import quote
from urllib.request import Request
import baseline_http as transport

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True)
    parser.add_argument("--password-file", type=Path, default=ROOT / ".tools/private/admin-password.txt")
    parser.add_argument("--out", type=Path, default=ROOT / ".tools/hardware/appliance-media")
    parser.add_argument("--cleanup-owned-report", type=Path, help="Remove only failed-run photos explicitly recorded by that private harness report")
    args = parser.parse_args()
    socket.inet_aton(args.address)
    args.out.mkdir(parents=True, exist_ok=True)
    base = "http://" + args.address
    password = args.password_file.read_text(encoding="utf-8").rstrip("\r\n")
    cookie = csrf = ""
    created = []
    report = dict(passed=False, checks=[], credentials_or_tokens_saved=False, originals_preserved=False)
    slow = None

    def expect(ok, name):
        report["checks"].append(dict(name=name, passed=bool(ok)))
        print(f"MEDIA {'PASS' if ok else 'FAIL'} {name}", flush=True)
        if not ok:
            raise RuntimeError("Media check failed: " + name)

    def request(path, method="GET", value=None, authorized=True, headers=None, raw=None):
        supplied = {"Cookie": cookie} if authorized and cookie else {}
        if method == "POST":
            supplied.update({"X-CSRF-Token": csrf, "Origin": base})
        supplied.update(headers or {})
        data = json.dumps(value).encode() if value is not None else raw
        if data is not None:
            supplied["Content-Type"] = "application/json"
        try:
            response = transport.HTTP.open(Request(base+path, data=data, headers=supplied, method=method), timeout=8)
        except HTTPError as error:
            response = error
        with response:
            body = response.read(250000)
            if password.encode() in body:
                raise RuntimeError("Private password in media response")
            return response.code, dict(response.headers), body

    def state(path="/api/status"):
        code, _, body = request(path)
        if code != 200:
            raise RuntimeError("State endpoint unavailable")
        return json.loads(body)

    def login():
        nonlocal cookie, csrf
        code, _, body = request("/api/auth/login", "POST", {"password": password}, headers={"X-Appliance-Request": "1"})
        expect(code == 202, "product_login_accepted")
        ticket = json.loads(body)["ticket"]
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            code, headers, body = request("/api/auth/result", headers={"X-Login-Ticket": ticket})
            if code == 202:
                time.sleep(.7)
                continue
            expect(code == 200, "product_login_verified")
            cookie = headers["Set-Cookie"].split(";", 1)[0]; csrf = json.loads(body)["csrf"]
            return
        raise RuntimeError("Product login timeout")

    def inventory():
        dates = []
        cursor = ""
        for _ in range(10):
            page = state("/api/media/dates" + ("?before=" + cursor if cursor else ""))
            if page["enumerationTruncated"]:
                raise RuntimeError("Inventory enumeration is incomplete")
            dates.extend(item["date"] for item in page["items"])
            if not page["more"]:
                break
            cursor = page["nextBefore"]
        else:
            raise RuntimeError("Bounded date pages exceeded")
        result = {}
        for date in dates:
            cursor = ""
            for _ in range(10):
                page = state("/api/media?date=" + date + ("&before=" + quote(cursor) if cursor else ""))
                if page["enumerationTruncated"]:
                    raise RuntimeError("File enumeration is incomplete")
                for item in page["items"]:
                    if item["path"] in result:
                        raise RuntimeError("Duplicate file across pages")
                    result[item["path"]] = item
                if not page["more"]:
                    break
                cursor = page["nextBefore"]
            else:
                raise RuntimeError("Bounded file pages exceeded")
        return result

    def download(path, output):
        req = Request(base + "/api/media/download?path=" + quote(path, safe=""), headers={"Cookie": cookie})
        with transport.HTTP.open(req, timeout=10) as response, output.open("wb") as file:
            if response.code != 200:
                raise RuntimeError("Download refused")
            if "attachment" not in response.headers.get("Content-Disposition", ""):
                raise RuntimeError("Missing download disposition")
            while chunk := response.read(65536):
                file.write(chunk)
        return output.stat().st_size

    class SlowDownload:
        def __init__(self, path):
            self.path = path; self.cookie = cookie
            self.ready, self.release, self.done = threading.Event(), threading.Event(), threading.Event()
            self.bytes = 0; self.complete = False; self.error = ""
            self.thread = threading.Thread(target=self.run, daemon=True)

        def run(self):
            connection = http.client.HTTPConnection(args.address, timeout=6)
            try:
                connection.connect()
                connection.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
                connection.request("GET", "/api/media/download?path=" + quote(self.path, safe=""), headers={"Cookie": self.cookie})
                response = connection.getresponse()
                if response.status != 200:
                    raise RuntimeError("Slow download refused")
                self.bytes += len(response.read(32)); self.ready.set()
                self.release.wait(5)
                while chunk := response.read(4096):
                    self.bytes += len(chunk)
                self.complete = True
            except http.client.IncompleteRead as error:
                self.bytes += len(error.partial); self.error = "revoked_incomplete_body"
            except Exception as error:
                self.error = type(error).__name__
            finally:
                connection.close(); self.cookie = ""; self.done.set()

    try:
        for path in ("/api/media/dates", "/api/media?date=20261003", "/api/media/download?path=/data/configs.txt"):
            expect(request(path, authorized=False)[0] == 401, "media_requires_session_" + path.split("?")[0])
        expect(request("/api/camera/photo", "POST", authorized=False)[0] == 403, "photo_requires_session_and_csrf")
        login()
        initial = state()
        report["initial_camera_ready"] = initial["cameraReady"]
        report["initial_startup_failed"] = initial["startupFailed"]
        if not initial["cameraReady"]:
            expect(request("/api/device/restart", "POST")[0] == 202, "normal_camera_recovery_restart")
            time.sleep(5)
            until = time.monotonic() + 30
            while time.monotonic() < until:
                try:
                    if request("/", authorized=False)[0] == 200:
                        break
                except OSError:
                    pass
                time.sleep(.5)
            cookie = csrf = ""; login(); initial = state()
        expect(initial["cameraReady"] and initial["sdReady"], "camera_and_sd_ready")
        if initial["recording"] or initial["recordRequested"]:
            raise RuntimeError("Existing recording must finish before media gate")
        if args.cleanup_owned_report:
            source = args.cleanup_owned_report.resolve()
            if not source.is_relative_to((ROOT / ".tools/hardware").resolve()):
                raise RuntimeError("Owned cleanup report must be a private local hardware report")
            previous = json.loads(source.read_text(encoding="utf-8"))
            paths = previous.get("owned_photo_paths", [])
            if previous.get("credentials_or_tokens_saved") is not False or not 1 <= len(paths) <= 22:
                raise RuntimeError("Owned cleanup report does not match the photo harness")
            for path in paths:
                if not re.fullmatch(r"/\d{8}/\d{8}_\d{6}_[a-f0-9]{6}_photo\.jpg", path):
                    raise RuntimeError("Owned cleanup path does not match generated photos")
                if request("/api/media/delete", "POST", {"path": path})[0] not in (200, 404):
                    raise RuntimeError("Previous owned photo cleanup refused")
            expect(True, "previous_failed_run_owned_photos_cleaned")
        originals = inventory()
        report["original_paths"] = sorted(originals)
        for path in ("/data/configs.txt", "/20261003/../../data/configs.txt", "/20261003/20261003_000000_test.jpg\x00tail",
                     "/20261003/20261003_000000_test.jpg/extra", "/20261003/20261003_000000_test.jpg%2e"):
            expect(request("/api/media/file?path=" + quote(path, safe=""))[0] == 400, "reject_unsafe_download_path")
        expect(request("/api/media?date=20261003&unexpected=1")[0] == 400, "reject_unknown_list_parameter")
        expect(request("/api/media/dates?before=../")[0] == 400, "reject_directory_traversal")
        expect(request("/api/camera/photo?ignored=1", "POST")[0] == 400, "photo_rejects_query_controls")
        for _ in range(22):
            code, _, body = request("/api/camera/photo", "POST")
            if code != 201:
                raise RuntimeError("Photo save refused")
            saved = json.loads(body); created.append(saved["path"])
            if len(created) == 1:
                report["saved_photo_bytes"] = saved["bytes"]
        expect(len(set(created)) == 22, "twenty_two_unique_saved_jpegs")
        date = created[0][1:9]
        page = state("/api/media?date=" + date)
        expect(len(page["items"]) == 20 and page["more"], "bounded_first_page_and_cursor")
        all_files = inventory()
        expect(set(created).issubset(all_files) and set(originals).issubset(all_files), "all_photos_listed_originals_preserved")
        photo = created[0]
        code, headers, jpeg = request("/api/media/file?path=" + quote(photo, safe=""))
        expect(code == 200 and jpeg[:2] == b'\xff\xd8' and jpeg[-2:] == b'\xff\xd9'
               and headers["Content-Type"] == "image/jpeg", "saved_photo_preview_is_jpeg")
        (args.out / "saved-photo.jpg").write_bytes(jpeg)
        expect(download(photo, args.out / "downloaded-photo.jpg") == len(jpeg), "photo_download_matches_saved_size")
        expect(request("/api/media/delete", "POST", {"path": photo}, headers={"X-CSRF-Token": "wrong"})[0] == 403,
               "delete_requires_csrf")
        expect(request("/api/media/delete", "POST", {"path": "/data/configs.txt"})[0] == 400, "delete_cannot_target_config")
        before_record = inventory()
        expect(request("/api/record/start", "POST")[0] == 202, "owned_media_recording_started")
        time.sleep(2)
        expect(state()["recording"], "owned_media_recording_active")
        expect(request("/api/media/delete", "POST", {"path": photo})[0] == 409, "delete_refused_while_recording")
        time.sleep(12)
        expect(request("/api/record/stop", "POST")[0] == 202, "owned_media_recording_stop")
        until = time.monotonic() + 25
        while time.monotonic() < until and state()["recording"]:
            time.sleep(.4)
        expect(not state()["recording"], "owned_media_recording_finalized")
        after_record = inventory()
        videos = [path for path in set(after_record) - set(before_record) if path.endswith(".avi")]
        expect(len(videos) == 1, "one_owned_video_added_to_media")
        video = videos[0]; report["owned_video_path"] = video
        expect(download(video, args.out / "owned-recording.avi") == after_record[video]["bytes"], "owned_video_download_complete")
        # Independent validation of the earlier camera/network-trial AVI.
        earlier = [path for path in originals if "_135625_" in path and path.endswith(".avi")]
        if earlier:
            expect(download(earlier[0], args.out / "network-trial-recording.avi") == originals[earlier[0]]["bytes"],
                   "network_trial_avi_download_complete")
        slow = SlowDownload(video); slow.thread.start()
        expect(slow.ready.wait(8), "slow_download_started")
        expect(request("/api/media/delete", "POST", {"path": video})[0] == 409, "owned_download_target_delete_refused")
        expect(state()["cameraReady"], "status_responsive_during_download")
        expect(request("/api/auth/logout", "POST")[0] == 200, "logout_during_download")
        cookie = csrf = ""; started = time.monotonic(); slow.release.set()
        expect(slow.done.wait(10) and slow.bytes < after_record[video]["bytes"] and not slow.complete,
               "logout_revokes_existing_download")
        report["revoked_download_bytes"] = slow.bytes
        report["revocation_seconds"] = round(time.monotonic() - started, 3)
        login()
        for path in created[1:]:
            if path in originals:
                raise RuntimeError("Refusing to delete pre-existing media")
            if request("/api/media/delete", "POST", {"path": path})[0] != 200:
                raise RuntimeError("Owned photo cleanup failed")
        final = inventory()
        expect(set(originals).issubset(final) and all(final[path]["bytes"] == originals[path]["bytes"] for path in originals),
               "every_original_file_preserved_with_same_size")
        expect(set(created).intersection(final) == {created[0]} and video in final, "owned_cleanup_keeps_one_photo_and_video")
        expect(request("/api/auth/logout", "POST")[0] == 200, "final_media_logout")
        cookie = csrf = ""
        report.update(passed=True, check_count=len(report["checks"]), originals_preserved=True,
                      kept_photo_path=created[0], original_count=len(originals))
    except Exception as error:
        report.update(error_type=type(error).__name__, check_count=len(report["checks"]), owned_photo_paths=created)
        if isinstance(error, RuntimeError):
            report["error_code"] = str(error)
        print("MEDIA stopped: " + type(error).__name__, flush=True)
    finally:
        if slow:
            slow.release.set()
        if cookie:
            try:
                request("/api/record/stop", "POST")
                request("/api/auth/logout", "POST")
            except OSError:
                pass
        password = cookie = csrf = ""
        (args.out / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key not in ("checks", "original_paths", "owned_photo_paths")}, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
