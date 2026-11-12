#!/usr/bin/env python3
"""Put the gallery on a real CRTC and check that every frame reached the panel unchanged.

The KMS backend cannot be unit tested: what it promises is about a display, and a CI runner has
none. This boots one. QEMU's virt machine gets a virtio-gpu, Alpine's arm64 `virt` kernel drives
it, and the gallery - built static for aarch64 - is the initramfs's /init. It shows every scene,
printing a digest of each frame's colour; after each one this script takes a QMP screendump of
what the virtual panel is scanning out and compares the two. A match is the whole path: the
modeset, the copy into the dumb buffer, the damage the buffer was owed, and the page flip.

    cmake -S . -B build/arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release \\
          -DCMAKE_C_COMPILER=... (any static aarch64 Linux toolchain with the kernel headers) \\
          -DINKWELL_WITH_TLS=OFF -DINKCELL_WITH_SDL=OFF -DINKCELL_BUILD_TESTS=OFF \\
          -DCMAKE_EXE_LINKER_FLAGS=-static
    cmake --build build/arm64 --target inkcell_gallery
    scripts/kms-vm.py build/arm64/examples/gallery/inkcell_gallery            # atomic
    scripts/kms-vm.py build/arm64/examples/gallery/inkcell_gallery --legacy   # SETCRTC + PAGE_FLIP

Needs qemu-system-aarch64, and tar and cpio on the PATH. The kernel and busybox are fetched from
Alpine's mirror into build/kms-vm/ once. Exits 0 when every frame matched.
"""

import argparse
import gzip
import io
import json
import os
import platform
import shutil
import socket
import subprocess
import sys
import tarfile
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOLD_MS = 1000  # how long the gallery holds each scene
MIRROR = "https://dl-cdn.alpinelinux.org/alpine/v3.20/main/aarch64"

INIT = """#!/bin/busybox sh
/bin/busybox --install -s /bin
mount -t proc proc /proc; mount -t sysfs sys /sys; mount -t devtmpfs dev /dev
modprobe virtio-gpu || echo "modprobe failed"
for i in 1 2 3 4 5 6 7 8 9 10; do [ -e /dev/dri/card0 ] && break; sleep 0.2; done
grep -q kms_legacy /proc/cmdline && export INKCELL_GALLERY_KMS_LEGACY=1
echo "GALLERY START"
/gallery --show kms --hold %d 2>&1
echo "GALLERY EXIT $?"
poweroff -f
"""


def fetch_package(name, work):
    """The named package from Alpine's index, unpacked into work/<name>/."""
    dest = os.path.join(work, name)
    if os.path.isdir(dest):
        return dest
    index = urllib.request.urlopen(f"{MIRROR}/APKINDEX.tar.gz").read()
    with tarfile.open(fileobj=io.BytesIO(index)) as tar:
        text = tar.extractfile("APKINDEX").read().decode()
    version = None
    for record in text.split("\n\n"):
        fields = dict(line.split(":", 1) for line in record.splitlines() if ":" in line)
        if fields.get("P") == name:
            version = fields["V"]
    if version is None:
        raise SystemExit(f"kms-vm: {name} is not in {MIRROR}")
    apk = os.path.join(work, f"{name}-{version}.apk")
    print(f"kms-vm: fetching {name} {version}", flush=True)
    urllib.request.urlretrieve(f"{MIRROR}/{name}-{version}.apk", apk)
    os.makedirs(dest)
    # An apk is several gzip streams end to end; tar reads through all of them.
    subprocess.run(["tar", "-xzf", apk, "-C", dest], check=True, stderr=subprocess.DEVNULL)
    return dest


def build_initramfs(gallery, kernel_tree, busybox_tree, work):
    root = os.path.join(work, "root")
    shutil.rmtree(root, ignore_errors=True)
    for d in ("bin", "proc", "sys", "dev"):
        os.makedirs(os.path.join(root, d))
    shutil.copy(os.path.join(busybox_tree, "bin/busybox.static"), os.path.join(root, "bin/busybox"))
    shutil.copy(gallery, os.path.join(root, "gallery"))
    with open(os.path.join(root, "init"), "w") as f:
        f.write(INIT % HOLD_MS)
    os.chmod(os.path.join(root, "init"), 0o755)

    # virtio-gpu and everything it needs, decompressed so busybox's modprobe can load them.
    (release,) = os.listdir(os.path.join(kernel_tree, "lib/modules"))
    src = os.path.join(kernel_tree, "lib/modules", release)
    deps = {}
    with open(os.path.join(src, "modules.dep")) as f:
        for line in f:
            module, _, needs = line.partition(":")
            deps[module] = needs.split()
    wanted, queue = set(), ["kernel/drivers/gpu/drm/virtio/virtio-gpu.ko.gz"]
    while queue:
        module = queue.pop()
        if module not in wanted:
            wanted.add(module)
            queue.extend(deps.get(module, []))
    dst = os.path.join(root, "lib/modules", release)
    for module in wanted:
        os.makedirs(os.path.join(dst, os.path.dirname(module)), exist_ok=True)
        with gzip.open(os.path.join(src, module)) as i, open(os.path.join(dst, module[:-3]), "wb") as o:
            shutil.copyfileobj(i, o)
    with open(os.path.join(dst, "modules.dep"), "w") as f:
        for module in sorted(wanted):
            f.write(f"{module[:-3]}: {' '.join(d[:-3] for d in deps[module])}\n")

    image = os.path.join(work, "initramfs.gz")
    files = subprocess.run(["find", "."], cwd=root, capture_output=True, check=True).stdout
    cpio = subprocess.run(["cpio", "-o", "-H", "newc"], cwd=root, input=files,
                          capture_output=True, check=True).stdout
    with gzip.open(image, "wb") as f:
        f.write(cpio)
    return image


def accel():
    if sys.platform == "darwin" and platform.machine() == "arm64":
        return ["-accel", "hvf", "-cpu", "host"]
    if platform.machine() == "aarch64" and os.access("/dev/kvm", os.W_OK):
        return ["-accel", "kvm", "-cpu", "host"]
    return ["-accel", "tcg", "-cpu", "max"]


def qmp_connect(path):
    for _ in range(100):
        try:
            s = socket.socket(socket.AF_UNIX)
            s.connect(path)
            f = s.makefile("rw")
            f.readline()
            f.write(json.dumps({"execute": "qmp_capabilities"}) + "\n")
            f.flush()
            f.readline()
            return f
        except OSError:
            time.sleep(0.1)
    raise SystemExit("kms-vm: QEMU's QMP socket never came up")


def screendump(qmp, path):
    qmp.write(json.dumps({"execute": "screendump", "arguments": {"filename": path}}) + "\n")
    qmp.flush()
    while True:
        reply = json.loads(qmp.readline())
        if "return" in reply or "error" in reply:
            return reply


def ppm_digest(path):
    """Size and FNV-1a over the RGB bytes of a binary PPM - what the gallery prints."""
    data = open(path, "rb").read()
    fields, at = [], 0
    while len(fields) < 4:
        while data[at:at + 1].isspace():
            at += 1
        end = at
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[at:end])
        at = end
    at += 1
    width, height = int(fields[1]), int(fields[2])
    digest = 1469598103934665603
    for byte in data[at:at + width * height * 3]:
        digest = ((digest ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{width}x{height}", f"{digest:016x}"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("gallery", help="inkcell_gallery, static, for aarch64 Linux")
    parser.add_argument("--legacy", action="store_true", help="SETCRTC and PAGE_FLIP, not atomic")
    parser.add_argument("--work", default=os.path.join(ROOT, "build", "kms-vm"))
    args = parser.parse_args()

    os.makedirs(args.work, exist_ok=True)
    kernel = fetch_package("linux-virt", args.work)
    busybox = fetch_package("busybox-static", args.work)
    initramfs = build_initramfs(args.gallery, kernel, busybox, args.work)

    # Relative, because a Unix socket's path is short and a build tree's often is not.
    os.chdir(args.work)
    if os.path.exists("qmp.sock"):
        os.unlink("qmp.sock")
    append = "console=ttyAMA0 rdinit=/init loglevel=4" + (" kms_legacy" if args.legacy else "")
    qemu = subprocess.Popen(
        ["qemu-system-aarch64", "-M", "virt", *accel(), "-m", "1024", "-smp", "2",
         "-kernel", os.path.join(kernel, "boot/vmlinuz-virt"), "-initrd", initramfs,
         "-append", append, "-device", "virtio-gpu-pci", "-display", "none",
         "-serial", "stdio", "-monitor", "none", "-qmp", "unix:qmp.sock,server,nowait",
         "-no-reboot"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    qmp = qmp_connect("qmp.sock")

    matched, differed, status = 0, 0, None
    for raw in qemu.stdout:
        line = raw.decode(errors="replace").rstrip()
        if not line.startswith("["):  # the kernel's own lines
            print(line, flush=True)
        if line.startswith("GALLERY EXIT "):
            status = line.split()[-1]
            continue
        if not line.startswith("shown "):
            continue
        _, name, size, digest = line.split()
        shot = os.path.join(args.work, f"{name}.ppm")
        # The line is printed when the flip is asked for, not when it lands, and under TCG the
        # scanout can trail it by a long way. So the panel is looked at again until it shows the
        # frame or the scene's hold is nearly up - past that it may be the next scene's.
        deadline = time.monotonic() + HOLD_MS / 1000 * 0.9
        while True:
            time.sleep(0.1)
            reply = screendump(qmp, shot)
            if "error" in reply:
                break
            panel_size, panel_digest = ppm_digest(shot)
            same = (panel_size, panel_digest) == (size, digest)
            if same or time.monotonic() >= deadline:
                break
        if "error" in reply:
            print(f"  screendump failed: {reply['error']}")
            differed += 1
            continue
        matched += same
        differed += not same
        print(f"  panel {panel_size} {panel_digest} {'matches' if same else 'DIFFERS'}", flush=True)
    qemu.wait()
    mode = "legacy" if args.legacy else "atomic"
    print(f"kms-vm: {mode}: {matched} frame(s) matched the panel, {differed} did not; "
          f"the gallery exited {status if status is not None else 'without saying'}")
    # A run that matched what it showed but stopped early - a crash, a refused init - is a
    # failure too, so the gallery's own exit status counts.
    return 0 if matched and not differed and status == "0" else 1


if __name__ == "__main__":
    sys.exit(main())
