"""Bundle non-platform Core dependencies; never copy glibc or Qt."""
import pathlib, re, shutil, subprocess, sys
app = pathlib.Path(sys.argv[1]).resolve()
lib = app / 'lib'
lib.mkdir(exist_ok=True)
allowed = ('libleveldb.so.', 'libsnappy.so.', 'libevent-', 'libevent_core-', 'libevent_pthreads-', 'libevent_extra-', 'libsqlite3.so.')
def run(args):
    return subprocess.check_output([str(a) for a in args], text=True, stderr=subprocess.STDOUT)
for binary in ['qbitx', 'qbitx-cli']:
    deps = run(['ldd', app / binary])
    assert 'not found' not in deps, deps
    for soname, source in re.findall(r'\s*(\S+) => (/\S+) ', deps):
        if soname.startswith(allowed):
            target = lib / soname
            if pathlib.Path(source).resolve() != target.resolve():
                shutil.copy2(pathlib.Path(source).resolve(), target)
            target.chmod(0o755)
    run(['patchelf', '--set-rpath', '$ORIGIN/lib', app / binary])
for p in lib.iterdir():
    if p.is_file() and p.name.startswith(allowed):
        run(['patchelf', '--set-rpath', '$ORIGIN', p])
licenses = app / 'licenses'
licenses.mkdir(exist_ok=True)
for pattern in ['libleveldb*/copyright', 'libsnappy*/copyright', 'libevent-*/copyright', 'libsqlite3-0/copyright']:
    for source in pathlib.Path('/usr/share/doc').glob(pattern):
        shutil.copy2(source, licenses / (source.parent.name + '-copyright.txt'))
for binary in ['qbitx', 'qbitx-cli']:
    deps = run(['ldd', app / binary])
    assert 'not found' not in deps
    for soname, source in re.findall(r'\s*(\S+) => (/\S+) ', deps):
        if soname.startswith(allowed):
            assert pathlib.Path(source).resolve().parent == lib, deps
    assert '0.4.0' in run([app / binary, '--version'])
    print(binary + ': private LevelDB/Snappy/Event/SQLite dependencies verified')
assert any(lib.glob('libleveldb.so.*'))
