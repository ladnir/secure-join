#!/usr/bin/env python3
"""Observe a specific Median controller from Windows; stop only its benchmark
children if host physical memory falls below the stated reserve. No application
or unrelated WSL process is closed. This sidecar never supplies paper timings.
"""
import argparse, ctypes, datetime, json, pathlib, subprocess, time

class MemoryStatus(ctypes.Structure):
    _fields_ = [("length",ctypes.c_uint32),("load",ctypes.c_uint32)] + [
        (name,ctypes.c_uint64) for name in ("total_phys","avail_phys","total_page",
                                          "avail_page","total_virtual","avail_virtual","extended")]

def available_memory():
    status=MemoryStatus(); status.length=ctypes.sizeof(status)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
        raise ctypes.WinError()
    return status.avail_phys

# Arguments are passed as separate argv entries. Read /proc to establish the
# controller identity and immediate child ownership before sending any signal.
LINUX = r"""
import json, os, pathlib, signal, sys
pid=int(sys.argv[1]); stop=sys.argv[2]=='stop'; root=pathlib.Path(sys.argv[3])
proc=pathlib.Path('/proc'); parent=proc/str(pid)
try: cmd=parent.joinpath('cmdline').read_bytes().decode().split(chr(0))
except FileNotFoundError:
 print(json.dumps({'active':False}));sys.exit(0)
if not any(cmd):
 print(json.dumps({'active':False}));sys.exit(0)
runner=str(root/'scripts/run_paper_benchmarks.py')
params=sys.argv[4]
if (root/'docs/benchmarks/paper-2026/median-retune-20260911').resolve() not in pathlib.Path(params).resolve().parents:
 raise RuntimeError('Parameters must stay inside the Median revision')
def option(name):return cmd[cmd.index(name)+1] if name in cmd else None
if runner not in cmd or option('--methods')!='median' or option('--parameters')!=params:
 raise RuntimeError('Controller identity mismatch; refusing to attach')
if option('--shapes')!='balanced' or option('--min-exp') not in ['19','20'] or option('--max-exp') not in ['19','20']:
 raise RuntimeError('This guard is restricted to the two largest Median sizes')
rows=[]; expected=(root/'out/build/linux/frontend/paper_benchmark').resolve()
for entry in proc.iterdir():
 if not entry.name.isdigit():continue
 try:
  fields=dict(l.split(':',1) for l in (entry/'status').read_text().splitlines() if ':' in l)
  if int(fields['PPid'])!=pid or (entry/'exe').resolve()!=expected:continue
  args=(entry/'cmdline').read_bytes().decode().split(chr(0))
  if args[args.index('--method')+1]!='median' or int(args[args.index('--n')+1]) not in [1<<19,1<<20]:
   raise RuntimeError('Unexpected benchmark child')
  row=dict(pid=int(entry.name),n=int(args[args.index('--n')+1]),
           rss_kib=int(fields.get('VmRSS','0 kB').split()[0]),
           swap_kib=int(fields.get('VmSwap','0 kB').split()[0]))
  rows.append(row)
  if stop:os.kill(row['pid'],signal.SIGTERM)
 except (FileNotFoundError,ProcessLookupError):continue
mem=dict(l.split(':',1) for l in (proc/'meminfo').read_text().splitlines())
print(json.dumps(dict(active=True,children=rows,stopped=bool(stop and rows),
                     linux_available_kib=int(mem['MemAvailable'].split()[0]))))
"""

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--controller',type=int,required=True)
    ap.add_argument('--min-free-gib',type=float,default=2)
    ap.add_argument('--observe-only',action='store_true',help='Record memory without stopping benchmark children')
    ap.add_argument('--parameters',type=pathlib.Path)
    ap.add_argument('--out',type=pathlib.Path,required=True)
    a=ap.parse_args()
    if not 1<=a.min_free_gib<=4:raise ValueError('Reserve must be between 1 and 4 GiB')
    root=pathlib.Path(__file__).resolve().parents[1]
    drive=root.drive[0].lower(); linux_root='/mnt/'+drive+root.as_posix()[2:]
    params=(a.parameters or root/'docs/benchmarks/paper-2026/median-retune-20260911/parameters.json').resolve()
    if (root/'docs/benchmarks/paper-2026/median-retune-20260911').resolve() not in params.parents or not params.is_file():
        raise ValueError('Parameters must be an existing file inside the Median revision')
    linux_params='/mnt/'+drive+params.as_posix()[2:]
    a.out.parent.mkdir(parents=True,exist_ok=True)
    next_probe=0
    while True:
        available=available_memory(); stop=not a.observe_only and available<a.min_free_gib*2**30
        if stop or time.monotonic()>=next_probe:
            result=subprocess.run(['wsl','-d','Ubuntu','-u','root','--','python3','-c',LINUX,
                                   str(a.controller),'stop' if stop else 'read',linux_root,linux_params],
                                  text=True,capture_output=True,timeout=20)
            if result.returncode:raise RuntimeError(result.stderr)
            row=json.loads(result.stdout)
            row.update(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                       controller=a.controller,host_available_bytes=available,
                       minimum_free_gib=None if a.observe_only else a.min_free_gib,
                       monitor_only=a.observe_only)
            if stop:row['reason']='Host available physical memory below configured reserve; incomplete trial must not be published'
            with a.out.open('a') as f:f.write(json.dumps(row)+'\n')
            print(json.dumps(row),flush=True)
            if not row['active'] or row.get('stopped'):return
            next_probe=time.monotonic()+15
        time.sleep(1)

if __name__=='__main__':main()
