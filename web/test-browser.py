#!/usr/bin/env python3
"""Exercise the real WebAssembly preview; no mock renderer or game assets."""
import argparse
from contextlib import contextmanager
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import threading

from playwright.sync_api import sync_playwright, expect

ROOT = Path(__file__).resolve().parent


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, *_args):
        pass


@contextmanager
def server(url):
    if url:
        yield url
        return
    handler = partial(QuietHandler, directory=str(ROOT))
    http = ThreadingHTTPServer(("127.0.0.1", 0), handler)
    thread = threading.Thread(target=http.serve_forever, daemon=True)
    thread.start()
    try:
        yield f"http://127.0.0.1:{http.server_address[1]}/"
    finally:
        http.shutdown()
        http.server_close()
        thread.join()


def fixture():
    # Original eight-point fixture, deliberately not an F-Zero course.
    positions = [(0, 0), (110, 0), (190, 80), (190, 190),
                 (110, 270), (0, 270), (-80, 190), (-80, 80)]
    points = [{"position": {"x": x * 65536, "y": 0, "z": z * 65536},
               "radius_left": 12 * 65536, "radius_right": 12 * 65536,
               "bank": 0, "track_segment_info": 0, "tension": 32768}
              for x, z in positions]
    return {"schema_version": 1, "coordinate_scale": 16,
            "count": len(points), "points": points,
            "provenance": {"course_name": "Browser test circuit",
                           "input_kind": "synthetic-test-fixture"}}


def pixels(page):
    return page.locator("#game").evaluate("canvas => canvas.toDataURL()")


def upload(page, name, data):
    page.locator("#course-file").set_input_files(
        {"name": name, "mimeType": "application/json",
         "buffer": json.dumps(data).encode("utf-8")})


def run(url, output):
    errors = []
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True, args=["--no-sandbox"])
        page = browser.new_page(viewport={"width": 1440, "height": 1200}, device_scale_factor=1)
        page.on("pageerror", lambda error: errors.append(str(error)))
        page.goto(url)
        page.wait_for_selector('html[data-ready="true"]')
        expect(page.locator("#failure")).to_be_hidden()
        expect(page.locator("#play-state")).to_have_text("Ready")
        assert int(page.locator("#polygons").inner_text()) > 0, "Initial track must render"
        expect(page.locator("#simulation")).to_have_text("PROTO")
        expect(page.locator("#simulation-note")).to_contain_text("not the original physics")
        initial = pixels(page)

        page.locator("#start").click()
        page.keyboard.down("ArrowUp")
        page.wait_for_function("Number(document.getElementById('speed').textContent) > 5")
        page.keyboard.down("ArrowRight")
        page.wait_for_timeout(250)
        page.keyboard.up("ArrowRight")
        page.keyboard.up("ArrowUp")
        page.keyboard.press("Escape")
        expect(page.locator("#play-state")).to_have_text("Paused")
        driven = pixels(page)
        assert driven != initial, "Driving must change actual rendered geometry"

        # The renderer path comparison must preserve geometry at a fixed pose.
        page.locator("#transform-mode").select_option("0")
        assert pixels(page) == driven, "General and prepared transforms must agree"
        page.locator("#transform-mode").select_option("1")
        assert pixels(page) == driven, "Returning to prepared path must preserve geometry"
        page.screenshot(path=str(output / "desktop.png"), full_page=True)

        # Pointer activation of Pause must not resume via the focus-loss handler.
        page.locator("#play").click()
        expect(page.locator("#play-state")).to_have_text("Driving")
        page.locator("#play").click()
        expect(page.locator("#play-state")).to_have_text("Paused")
        page.locator("#reset").click()
        expect(page.locator("#speed")).to_have_text("0")
        assert pixels(page) == initial, "Reset must restore the original pose"

        requests = []
        page.on("request", lambda request: requests.append(request.url))
        upload(page, "test-course.json", fixture())
        expect(page.locator("#course-name")).to_have_text("Browser test circuit")
        expect(page.locator("#feedback")).to_have_text("Loaded 8 control points locally.")
        assert not requests, "Local course import must not send network requests"
        imported = pixels(page)
        assert imported != initial, "Import must replace the rendered course"

        invalid = fixture()
        invalid["count"] = 65
        upload(page, "bad-course.json", invalid)
        expect(page.locator("#feedback")).to_have_class("feedback error")
        assert pixels(page) == imported, "Invalid import must retain the active course"

        page.locator("#use-demo").click()
        expect(page.locator("#course-name")).to_have_text("Test circuit")
        assert pixels(page) == initial, "Test-circuit control must restore original geometry"

        mobile = browser.new_page(viewport={"width": 390, "height": 844},
                                  device_scale_factor=1, is_mobile=True, has_touch=True,
                                  reduced_motion="reduce")
        mobile.on("pageerror", lambda error: errors.append(str(error)))
        mobile.goto(url)
        mobile.wait_for_selector('html[data-ready="true"]')
        expect(mobile.locator("#play-state")).to_have_text("Ready")
        assert mobile.evaluate("document.documentElement.scrollWidth <= innerWidth"), "Mobile layout overflows"
        expect(mobile.locator(".touch-controls")).to_be_visible()
        accelerator = mobile.locator('[data-control="4"]')
        accelerator.scroll_into_view_if_needed()
        box = accelerator.bounding_box()
        mobile.mouse.move(box["x"] + box["width"] / 2, box["y"] + box["height"] / 2)
        mobile.mouse.down()
        mobile.wait_for_function("Number(document.getElementById('speed').textContent) > 5")
        mobile.mouse.up()
        expect(accelerator).to_have_attribute("aria-pressed", "false")
        mobile.locator("#play").click()
        expect(mobile.locator("#play-state")).to_have_text("Paused")
        mobile.screenshot(path=str(output / "mobile.png"), full_page=True)
        assert not errors, "Browser errors: " + "; ".join(errors)
        browser.close()
    print("PASS: real WASM drive/reset, exact transform switch, local import/rejection, mobile controls/layout")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", help="Test an existing hosted preview instead of the local build")
    parser.add_argument("--output-dir", type=Path, default=ROOT.parent / "build" / "web-test")
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    with server(args.url) as base:
        run(base, args.output_dir)
