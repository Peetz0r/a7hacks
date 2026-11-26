#!/usr/bin/env python3
"""camsh: keeps a service-mode session with the camera open and takes commands over a Unix socket, so that not
every command costs a mode change (each one sends the camera back through its USB screens to mass storage mode).

  camsh.py serve                 log in (switching the camera to service mode) and wait for commands
  camsh.py sh 'COMMAND'          run COMMAND in the camera's shell (as root), print its output
  camsh.py pull REMOTE LOCAL     copy a file from the camera
  camsh.py shot OUT.png          screenshot of the Android screen (screencap into RAM, copied over, removed)
  camsh.py quit                  end the session properly (before an install, say)

The terminal is switched on once and left on; files are read while it is on. serve uses pmca (Sony-PMCA-RE in
~/Sony-PMCA-RE) as a library, since its own terminal only ends on a real Ctrl+C: run it with the Python that has
pmca's dependencies (its venv, say). The other commands only need Python. The socket is in $XDG_RUNTIME_DIR."""
import json, os, re, socket, sys, time

SOCK = os.path.join(os.environ.get('XDG_RUNTIME_DIR') or '/tmp', 'camsh.sock')
SHOT_REMOTE = '/android/dev/a7shot.png'      # /android/dev is tmpfs: RAM
SHOT = ('chroot /android /system/bin/sh -c "ANDROID_ROOT=/system ANDROID_DATA=/data '
        'LD_LIBRARY_PATH=/system/lib /system/bin/screencap -p /dev/a7shot.png"')


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
                        elif req[0] == 'shot':
                            out, rc = sh(cam, 'rm -f %s; %s' % (SHOT_REMOTE, SHOT))
                            if rc: raise Exception('screencap: rc %d %s' % (rc, out))
                            with open(req[1], 'wb') as f: b.readFile(SHOT_REMOTE, f)
                            sh(cam, 'rm -f ' + SHOT_REMOTE)
                            res = {'ok': '%d bytes to %s' % (os.path.getsize(req[1]), req[1])}
                        else:
                            res = {'error': 'unknown command'}
                    except Exception as e:
                        res = {'error': repr(e)}
                    conn.sendall((json.dumps(res) + '\n').encode())
        finally:
            cam.setTerminalEnable(False)
            if os.path.lexists(SOCK): os.remove(SOCK)

    senserShellCommand(complete=run)


def client(args):
    if args[0] in ('pull', 'shot'):
        args = args[:-1] + [os.path.abspath(args[-1])]
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(SOCK)
    s.sendall((json.dumps(args) + '\n').encode())
    res = json.loads(s.makefile().readline())
    if 'out' in res:
        sys.stdout.write(res['out'])
        return res['rc']
    print(res.get('ok') or 'error: ' + res.get('error', '?'), file=sys.stderr if 'error' in res else sys.stdout)
    return 1 if 'error' in res else 0


if __name__ == '__main__':
    a = sys.argv[1:]
    usage = {'serve': 0, 'sh': 1, 'pull': 2, 'shot': 1, 'quit': 0}
    if not a or a[0] not in usage or len(a) != usage[a[0]] + 1:
        sys.exit(__doc__)
    sys.exit(serve() if a[0] == 'serve' else client(a))
