#!/usr/bin/env python3
"""Unpack the pinned Yaul container image into a local folder, without Docker.

    tools/fetch_yaul.py DEST

Fetches ijacquez/yaul at the digest CI uses (see .github/workflows/build.yml) from Docker Hub,
checks every layer's sha256 against the manifest, and extracts the layers in order into DEST
(whiteouts applied). Then build the Saturn image on Linux/WSL with:

    export YAUL_INSTALL_ROOT=DEST/opt/tool-chains/sh2eb-elf YAUL_ARCH_SH_PREFIX=sh2eb-elf ...
    (tools/saturn_build.sh sets these from the image's own environment)

Writes DEST/yaul-env.sh with the image's ENV lines.
"""
import hashlib
import io
import json
import os
import shutil
import sys
import tarfile
import urllib.request

REPO = "ijacquez/yaul"
DIGEST = "sha256:b6d272204767cd0a734139dd3a2ab298ef5912fb7b7224b78e6d9fac1a7825a6"
ACCEPT = ", ".join([
    "application/vnd.docker.distribution.manifest.v2+json",
    "application/vnd.docker.distribution.manifest.list.v2+json",
    "application/vnd.oci.image.manifest.v1+json",
    "application/vnd.oci.image.index.v1+json",
])


def get(url, token, accept=ACCEPT):
    req = urllib.request.Request(url, headers={"Authorization": "Bearer " + token, "Accept": accept,
                                               "User-Agent": "fzx-fetch-yaul"})
    return urllib.request.urlopen(req, timeout=600)


def main():
    dest = os.path.abspath(sys.argv[1])
    os.makedirs(dest, exist_ok=True)
    tok = json.load(urllib.request.urlopen(
        "https://auth.docker.io/token?service=registry.docker.io&scope=repository:%s:pull" % REPO))["token"]
    base = "https://registry-1.docker.io/v2/%s" % REPO
    manifest = json.load(get("%s/manifests/%s" % (base, DIGEST), tok))
    if "manifests" in manifest:  # an index: pick linux/amd64
        pick = [m for m in manifest["manifests"]
                if m.get("platform", {}).get("architecture") == "amd64" and m["platform"].get("os") == "linux"][0]
        manifest = json.load(get("%s/manifests/%s" % (base, pick["digest"]), tok))
    config = json.load(get("%s/blobs/%s" % (base, manifest["config"]["digest"]), tok, "*/*"))
    with open(os.path.join(dest, "yaul-env.sh"), "w") as f:
        for e in config.get("config", {}).get("Env", []):
            k, _, v = e.partition("=")
            f.write("export %s=%s\n" % (k, json.dumps(v)))
    for layer in manifest["layers"]:
        d = layer["digest"]
        print("layer", d[:19], layer.get("size"), flush=True)
        data = get("%s/blobs/%s" % (base, d), tok, "*/*").read()
        if "sha256:" + hashlib.sha256(data).hexdigest() != d:
            sys.exit("digest mismatch on " + d)
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:*") as tar:
            members = []
            for m in tar.getmembers():
                name = os.path.normpath(m.name)
                if name.startswith("..") or os.path.isabs(name):
                    continue
                head, base_name = os.path.split(name)
                if base_name == ".wh..wh..opq":
                    p = os.path.join(dest, head)
                    if os.path.isdir(p):
                        for c in os.listdir(p):
                            cp = os.path.join(p, c)
                            shutil.rmtree(cp) if os.path.isdir(cp) and not os.path.islink(cp) else os.remove(cp)
                    continue
                if base_name.startswith(".wh."):
                    p = os.path.join(dest, head, base_name[4:])
                    if os.path.islink(p) or os.path.isfile(p):
                        os.remove(p)
                    elif os.path.isdir(p):
                        shutil.rmtree(p)
                    continue
                if m.isdev():
                    continue
                target = os.path.join(dest, name)
                if (os.path.lexists(target) and not os.path.isdir(target)) or (m.isdir() is False and os.path.isdir(target) and not os.path.islink(target)):
                    if os.path.isdir(target) and not os.path.islink(target):
                        shutil.rmtree(target)
                    else:
                        os.remove(target)
                members.append(m)
            tar.extractall(dest, members=members, filter="tar" if hasattr(tarfile, "data_filter") else None)
    print("done", dest)


if __name__ == "__main__":
    main()
