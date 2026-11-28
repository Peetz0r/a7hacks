#!/usr/bin/env python3
"""camsh: keeps a service-mode session with the camera open and takes commands over a Unix socket, so that not
every command costs a mode change (each one sends the camera back through its USB screens to mass storage mode).

  camsh.py serve                 log in (switching the camera to service mode) and wait for commands
  camsh.py serve --updater       the same in updater mode (pmca's updatershell: a small system of its own, no
                                 Android, /android/data not mounted); quit ends it and the camera starts anew
  camsh.py sh 'COMMAND'          run COMMAND in the camera's shell (as root), print its output
  camsh.py pull REMOTE LOCAL     copy a file from the camera
  camsh.py push LOCAL REMOTE     copy a file to the camera (big ones to the SD card: /android/dev is RAM, ~4 MB free)
  camsh.py shot OUT.png          screenshot of the camera's screen as shown: Sony's OSD over the live view (the
                                 helper's --screenshot into RAM, copied over, removed)
  camsh.py shot OSD.png LV.png OUT.png
                                 the same, plus the OSD and the live view each on its own (via the SD card: three
                                 PNGs don't fit in the camera's free RAM)
  camsh.py shot --android OUT.png
                                 Android's own frame instead (screencap into RAM), in full colour: the panel (and
                                 so shot without --android) gets it cut to 4 bits per channel (v >> 4, RGBA4444)
  camsh.py quit                  end the session properly (before an install, say)

shot re-encodes the PNGs it copied (lossless; the camera writes them uncompressed): with optipng when that's
installed, else with Python's zlib.

The terminal is switched on once and left on; files are read while it is on. serve uses pmca (Sony-PMCA-RE in
~/Sony-PMCA-RE) as a library, since its own terminal only ends on a real Ctrl+C: run it with the Python that has
pmca's dependencies (its venv, say). The other commands only need Python. The socket is in $XDG_RUNTIME_DIR."""
import json, os, re, shutil, socket, struct, subprocess, sys, time, zlib

SOCK = os.path.join(os.environ.get('XDG_RUNTIME_DIR') or '/tmp', 'camsh.sock')
SHOT_REMOTE = '/android/dev/a7shot.png'      # /android/dev is tmpfs: RAM
SHOT = ('chroot /android /system/bin/sh -c "ANDROID_ROOT=/system ANDROID_DATA=/data '
        'LD_LIBRARY_PATH=/system/lib /system/bin/screencap -p /dev/a7shot.png"')
# the helper's --screenshot: the test copy when there is one (an install removes it, so it's the newer one)
LIBDIR = '/android/data/lib/nl.peetz0r.a7hacks/lib/'
HELPER = ('h=%sliba7test.so; [ -x $h ] || h=%sliba7hacks.so; echo "($h)"; $h --screenshot' % (LIBDIR, LIBDIR))
SHOT3_REMOTE = ['/android/storage/sdcard0/A7SHOT_O.PNG', '/android/storage/sdcard0/A7SHOT_L.PNG',
                '/android/storage/sdcard0/A7SHOT_B.PNG']   # SD card (8.3 names), removed afterwards


def sh(cam, cmd, timeout=60):
    """runs cmd in the camera's shell; returns its output and exit status"""
    # markers around the output; the echoed command line has "__A7""BEG" (quotes) instead, so it never matches
    cam.dev.writeTerminal(('echo "__A7""BEG"; ' + cmd + '; echo "__A7""END $?"\n').encode())
    buf, until = b'', time.time() + timeout
    while True:
        m = re.search(rb'__A7BEG\r?\n(.*?)__A7END (\d+)', buf, re.S)
        if m:
            return m.group(1).decode('latin1').replace('\r\n', '\n'), int(m.group(2))
        if time.time() > until:
            raise Exception('no answer within %d s' % timeout)
        d = cam.dev.readTerminal()
        if d: buf += d
        else: time.sleep(0.05)


def ush(b, cmd, timeout=60):
    """updater mode: runs cmd in a shell of its own (one per command: while a shell is open the USB channel is
    busy, so pull and push can't run alongside); returns its output and exit status"""
    import threading
    a, c = socket.socketpair()
    t = threading.Thread(target=b.interactiveShell, args=(a,), daemon=True)
    t.start()
    c.sendall(('echo "__A7""BEG"; ' + cmd + '; echo "__A7""END $?"; exit\n').encode())
    c.settimeout(timeout)
    buf = b''
    try:
        while True:                                  # until the shell has gone (exit) and the bridge closes
            d = c.recv(65536)
            if not d:
                break
            buf += d
    except socket.timeout:
        raise Exception('no answer within %d s' % timeout)
    finally:
        c.close()
        t.join(10)
    m = re.search(rb'__A7BEG\r?\n(.*?)__A7END (\d+)', buf, re.S)
    if not m:
        raise Exception('no answer: %r' % buf[-200:])
    return m.group(1).decode('latin1').replace('\r\n', '\n'), int(m.group(2))


def serve_updater():
    sys.path.insert(0, os.path.expanduser('~/Sony-PMCA-RE'))
    from pmca.commands.usb import updaterShellCommand
    from pmca.platform.backend.usb import UsbPlatformBackend

    def run(device):
        b = UsbPlatformBackend(device)
        b.start()
        try:
            if os.path.lexists(SOCK): os.remove(SOCK)
            srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            srv.bind(SOCK)
            srv.listen(1)
            print('ready (updater mode) on %s' % SOCK, flush=True)
            while True:
                conn, _ = srv.accept()
                with conn:
                    req = json.loads(conn.makefile().readline())
                    try:
                        if req[0] == 'quit':
                            conn.sendall(b'{"ok": "session ended"}\n')
                            return
                        if req[0] == 'sh':
                            out, rc = ush(b, req[1])
                            res = {'out': out, 'rc': rc}
                        elif req[0] == 'pull':
                            with open(req[2], 'wb') as f: b.readFile(req[1], f)
                            res = {'ok': '%d bytes to %s' % (os.path.getsize(req[2]), req[2])}
                        elif req[0] == 'push':
                            with open(req[1], 'rb') as f: b.writeFile(req[2], f)
                            res = {'ok': '%d bytes to %s' % (os.path.getsize(req[1]), req[2])}
                        else:
                            res = {'error': 'unknown command'}
                    except Exception as e:
                        res = {'error': repr(e)}
                    try:
                        conn.sendall((json.dumps(res) + '\n').encode())
                    except OSError:
                        pass
        finally:
            b.stop()                                 # leaves updater mode: the camera starts anew
            if os.path.lexists(SOCK): os.remove(SOCK)

    updaterShellCommand(complete=run)


def serve():
    sys.path.insert(0, os.path.expanduser('~/Sony-PMCA-RE'))
    from pmca.commands.usb import senserShellCommand
    from pmca.platform.backend.senser import SenserPlatformBackend

    def run(cam):
        b = SenserPlatformBackend(cam)
        b.start()
        cam.setTerminalEnable(False)
        cam.setTerminalEnable(True)
        try:
            time.sleep(1)
            while cam.dev.readTerminal(): pass       # the banner
            sh(cam, 'cd /')
            if os.path.lexists(SOCK): os.remove(SOCK)
            srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            srv.bind(SOCK)
            srv.listen(1)
            print('ready on %s' % SOCK, flush=True)
            while True:
                conn, _ = srv.accept()
                with conn:
                    req = json.loads(conn.makefile().readline())
                    try:
                        if req[0] == 'quit':
                            conn.sendall(b'{"ok": "session ended"}\n')
                            return
                        if req[0] == 'sh':
                            out, rc = sh(cam, req[1])
                            res = {'out': out, 'rc': rc}
                        elif req[0] == 'pull':
                            with open(req[2], 'wb') as f: b.readFile(req[1], f)
                            res = {'ok': '%d bytes to %s' % (os.path.getsize(req[2]), req[2])}
                        elif req[0] == 'push':
                            with open(req[1], 'rb') as f: b.writeFile(req[2], f)
                            res = {'ok': '%d bytes to %s' % (os.path.getsize(req[1]), req[2])}
                        elif req[0] == 'shot' and req[1] == '--android':
                            out, rc = sh(cam, 'rm -f %s; %s' % (SHOT_REMOTE, SHOT))
                            if rc: raise Exception('screencap: rc %d %s' % (rc, out))
                            with open(req[2], 'wb') as f: b.readFile(SHOT_REMOTE, f)
                            sh(cam, 'rm -f ' + SHOT_REMOTE)
                            res = {'ok': '%d bytes to %s' % (os.path.getsize(req[2]), req[2])}
                        elif req[0] == 'shot':
                            local = req[1:]
                            remote = SHOT3_REMOTE if len(local) == 3 else [SHOT_REMOTE]
                            out, rc = sh(cam, 'rm -f %s; %s %s' % (' '.join(remote), HELPER, ' '.join(remote)))
                            try:
                                if rc or 'written' not in out: raise Exception('screenshot: rc %d %s' % (rc, out.strip()))
                                for r, l in zip(remote, local):
                                    with open(l, 'wb') as f: b.readFile(r, f)
                            finally:
                                sh(cam, 'rm -f ' + ' '.join(remote))
                            res = {'ok': out.strip().replace('\n', ' ') + '\n' +
                                   ', '.join('%d bytes to %s' % (os.path.getsize(l), l) for l in local)}
                        else:
                            res = {'error': 'unknown command'}
                    except Exception as e:
                        res = {'error': repr(e)}
                    try:
                        conn.sendall((json.dumps(res) + '\n').encode())
                    except OSError:
                        pass                                 # the client left before the answer: keep serving
        finally:
            cam.setTerminalEnable(False)
            if os.path.lexists(SOCK): os.remove(SOCK)

    senserShellCommand(complete=run)


def _chunk(t, d):
    return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else b if pb <= pc else c


def repng_zlib(path):
    """re-encodes an 8-bit, non-interlaced PNG with the standard library: each row's filter chosen like libpng
    does (smallest sum of absolute values), zlib level 9; only IHDR, IDAT and IEND are kept"""
    b = open(path, 'rb').read()
    if b[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG')
    cs, i = [], 8
    while i < len(b):
        n, t = struct.unpack('>I4s', b[i:i + 8])
        cs.append((t, b[i + 8:i + 8 + n]))
        i += 12 + n
    ihdr = dict(cs)[b'IHDR']
    w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', ihdr)
    if depth != 8 or inter or ctype not in (0, 2, 4, 6):
        raise ValueError('only 8-bit, non-interlaced, non-palette PNGs')
    bpp = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
    stride = w * bpp
    raw = zlib.decompress(b''.join(d for t, d in cs if t == b'IDAT'))
    rows, prev = [], bytearray(stride)
    for y in range(h):                               # undo the file's filters
        f = raw[y * (stride + 1)]
        r = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = r[x - bpp] if x >= bpp else 0
            c = prev[x - bpp] if x >= bpp else 0
            r[x] = (r[x] + (0, a, prev[x], (a + prev[x]) >> 1, _paeth(a, prev[x], c))[f]) & 255
        rows.append(bytes(r))
        prev = r
    out, prev = bytearray(), bytes(stride)
    for r in rows:
        left, ul = bytes(bpp) + r[:-bpp], bytes(bpp) + prev[:-bpp]
        cands = (r,
                 bytes((a - b) & 255 for a, b in zip(r, left)),
                 bytes((a - b) & 255 for a, b in zip(r, prev)),
                 bytes((a - ((b + c) >> 1)) & 255 for a, b, c in zip(r, left, prev)),
                 bytes((x - _paeth(a, b, c)) & 255 for x, a, b, c in zip(r, left, prev, ul)))
        f, best = min(enumerate(cands), key=lambda t: sum(v if v < 128 else 256 - v for v in t[1]))
        out.append(f)
        out += best
        prev = r
    new = (b'\x89PNG\r\n\x1a\n' + _chunk(b'IHDR', ihdr) + _chunk(b'IDAT', zlib.compress(bytes(out), 9)) +
           _chunk(b'IEND', b''))
    if len(new) < len(b):
        with open(path + '.tmp', 'wb') as f:
            f.write(new)
        os.replace(path + '.tmp', path)


def repng(path):
    """smaller PNG, same pixels: optipng if it's installed, else repng_zlib; returns a note for the answer"""
    before = os.path.getsize(path)
    how = 'optipng'
    if shutil.which('optipng'):
        subprocess.run(['optipng', '-quiet', '-o2', path], check=True)
    else:
        how = 'zlib'
        repng_zlib(path)
    return '%s: %d -> %d bytes (%s)' % (path, before, os.path.getsize(path), how)


def client(args):
    if args[0] == 'pull':
        args = args[:-1] + [os.path.abspath(args[-1])]
    if args[0] == 'shot':
        args = args[:1] + [a if a == '--android' else os.path.abspath(a) for a in args[1:]]
    if args[0] == 'push':
        args = [args[0], os.path.abspath(args[1]), args[2]]
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(SOCK)
    s.sendall((json.dumps(args) + '\n').encode())
    res = json.loads(s.makefile().readline())
    if 'out' in res:
        sys.stdout.write(res['out'])
        return res['rc']
    print(res.get('ok') or 'error: ' + res.get('error', '?'), file=sys.stderr if 'error' in res else sys.stdout)
    if args[0] == 'shot' and 'error' not in res:
        for p in args[1:]:
            if p != '--android':
                print(repng(p))
    return 1 if 'error' in res else 0


if __name__ == '__main__':
    a = sys.argv[1:]
    usage = {'serve': 0, 'sh': 1, 'pull': 2, 'push': 2, 'shot': 1, 'quit': 0}
    shot = a[:1] == ['shot'] and (len(a) in (2, 4) and '--android' not in a or len(a) == 3 and a[1] == '--android')
    if not a or a[0] not in usage or (len(a) != usage[a[0]] + 1 and a != ['serve', '--updater'] and not shot) \
            or (a[0] == 'shot' and not shot):
        sys.exit(__doc__)
    sys.exit((serve_updater() if a[1:2] == ['--updater'] else serve()) if a[0] == 'serve' else client(a))
