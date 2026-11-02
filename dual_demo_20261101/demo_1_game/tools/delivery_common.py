"""Small stdlib-only helpers shared by the portable delivery scripts."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import time


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def contained(root, path):
    root, path = Path(root).resolve(), Path(path).resolve()
    if path != root and root not in path.parents:
        raise ValueError('Path escapes project: ' + str(path))
    return path


def hashes(root, paths):
    root = Path(root).resolve()
    return {contained(root, p).relative_to(root).as_posix(): sha256(p)
            for p in sorted(set(paths)) if p.is_file()}


def efinity_env(root):
    root = Path(root).resolve()
    env = os.environ.copy()
    for key, suffix in [('EFINITY_HOME', ''), ('EFX_HOME', ''), ('EFXPT_HOME', 'pt'),
                        ('EFXPGM_HOME', 'pgm'), ('EFXDBG_HOME', 'debugger'), ('EFXIPM_HOME', 'ipm')]:
        env[key] = (root / suffix).as_posix()
    env.update(PYTHONHOME=str(root / 'python311'), QT_PLUGIN_PATH=(root / 'python311').as_posix(),
               PYTHONUTF8='1', PYTHONIOENCODING='utf-8', PYTHONDONTWRITEBYTECODE='1',
               PROCESSOR_ARCHITECTURE='AMD64')
    env['PATH'] = os.pathsep.join(str(root / p) for p in
                                 ['python311/bin', 'bin', 'pgm/bin', 'debugger/bin', 'scripts']) + os.pathsep + env.get('PATH', '')
    env['PYTHONPATH'] = os.pathsep.join(str(root / p) for p in ['debugger/bin', 'pgm/bin'])
    if env.get('LOCALAPPDATA'):
        env['EFINITY_USER_DIR_INI'] = (Path(env['LOCALAPPDATA']) / 'efinity/user_dir.ini').as_posix()
    return env


def run_logged(args, cwd, env, log, timeout=None):
    start = time.monotonic()
    with Path(log).open('xb') as stream:
        stream.write((subprocess.list2cmdline([str(a) for a in args]) + '\n').encode('utf-8'))
        stream.flush()
        try:
            proc = subprocess.run(args, cwd=cwd, env=env, stdout=stream, stderr=subprocess.STDOUT,
                                  timeout=timeout, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            code, error = proc.returncode, None
        except subprocess.TimeoutExpired:
            code, error = 124, 'Child process exceeded explicit timeout'
        except OSError as exc:
            code, error = 127, str(exc)
    return dict(args=[str(a) for a in args], cwd=str(cwd), returncode=code,
                seconds=round(time.monotonic() - start, 3), error=error, log=str(log))
