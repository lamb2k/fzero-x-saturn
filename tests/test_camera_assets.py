import importlib.util
from pathlib import Path
import struct
import os
import subprocess
import tempfile
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import camera_assets as camera


class CameraAssets(unittest.TestCase):
    def fixture(self):
        source = "".join("aVp%s:\n  type: VP\n  offset: 0x%X\n  symbol: aVp%s\n" %
                         (name, i*16, name) for i, name in enumerate(camera.NAMES))
        segment = b"".join(struct.pack(">8h", 100+i, 80+i, 511, 0, -30+i, 40+i, 511, 0)
                           for i in range(13))
        return segment, source

    def test_all_records_and_endian_header(self):
        segment, source = self.fixture()
        rows = camera.decode(segment, source)
        self.assertEqual(rows[12], (112, 92, 511, 0, -18, 52, 511, 0))
        generated = camera.header(rows)
        self.assertEqual(generated.count("__attribute__((aligned(8)))"), 13)
        self.assertIn("VP_HALF(65506)", generated)
        self.assertIn("__ORDER_BIG_ENDIAN__", generated)

    def test_bad_span_layout_and_scale(self):
        segment, source = self.fixture()
        for data, text in ((segment[:-1], source), (segment, source.replace("aVpFullScreen", "aVpWrong")),
                           (b"\0\0"+segment[2:], source)):
            with self.assertRaises(ValueError):
                camera.decode(data, text)
        with self.assertRaises(ValueError):
            camera.header([])

    @unittest.skipIf(os.name == "nt", "receipt shell check runs in Linux")
    def test_receipt_rejects_mutation(self):
        root=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temp:
            out=Path(temp)
            env=dict(os.environ,FZX_EVIDENCE=temp)
            subprocess.run([sys.executable,str(root/"tools/camera_assets.py"),"--art","fixture","--output",temp],
                           env=env,check=True,stdout=subprocess.DEVNULL)
            command=["bash",str(root/"tools/check_receipt.sh"),str(out/"viewports.receipt")]
            subprocess.run(command,check=True,stdout=subprocess.DEVNULL)
            with (out/"viewports.h").open("ab") as f: f.write(b"/* test mutation */\n")
            result=subprocess.run(command,capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn("stale: viewports.h",result.stderr)
