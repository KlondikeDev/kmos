# This file has been generated in whole or in part by a Large Language Model
"""Live update expectations must follow the ISO, not mutable staging/disk files."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('update_live_test',ROOT/'buildutils/update_live_test.py')
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory(prefix='kunix-live-inputs-') as temp:
    work=Path(temp)
    boot=work/'tree/boot';boot.mkdir(parents=True)
    (boot/'root.img').write_bytes(b'pristine boot root')
    (boot/'kunix-kodiak.elf').write_bytes(b'pristine boot kernel')
    iso=work/'live.iso'
    subprocess.run(['xorriso','-as','mkisofs','-o',str(iso),str(work/'tree')],
                   check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    # A later build or VM writes those files. The existing ISO stays immutable.
    (boot/'root.img').write_bytes(b'changed by a later build')
    (work/'ufs.img').write_bytes(b'changed by a filesystem test')
    root=module.extract_live_payload(iso,'root.img',work/'root.expected')
    kernel=module.extract_live_payload(iso,'kunix-kodiak.elf',work/'kernel.expected')
    assert root.read_bytes()==b'pristine boot root'
    assert kernel.read_bytes()==b'pristine boot kernel'
print('live update inputs: expected payloads extracted from immutable boot ISO passed')
