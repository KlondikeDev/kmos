#!/usr/bin/env python3
# This file has been generated in whole or in part by a Large Language Model
"""Bootstrap an existing disposable installation from live media, without network or reinstall."""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parent.parent


def digest_range(image, start, length):
    digest=hashlib.sha256()
    with image.open('rb') as source:
        source.seek(start)
        while length:
            data=source.read(min(length, 1024*1024))
            if not data:
                raise RuntimeError('short disk partition')
            digest.update(data)
            length-=len(data)
    return digest.digest()


def extract_live_payload(iso, name, destination):
    """Read the booted artifact, not ufs.img (which other VM tests modify)."""
    subprocess.run(['xorriso', '-osirrox', 'on', '-indev', str(iso),
                    '-extract', '/boot/' + name, str(destination)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    return destination


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image',type=Path,required=True,help='existing seeded disposable NVMe fixture; copied before use')
    parser.add_argument('--block-size',type=int,choices=(512,4096),default=512)
    parser.add_argument('--iso',type=Path,default=ROOT/'build/binaries/kunix-live.iso')
    parser.add_argument('--root-image',type=Path,help='expected root override; default extracts it from the ISO')
    parser.add_argument('--kernel',type=Path,help='expected kernel override; default extracts it from the ISO')
    args=parser.parse_args()
    # Limit source selection to prior integration fixtures, never a host device.
    base=ROOT/'build/phase5';base.mkdir(exist_ok=True,parents=True)
    if not args.image.is_file() or not args.image.resolve().is_relative_to(base.resolve()):
        parser.error('--image must be a disposable integration fixture under build/phase5')
    work=Path(tempfile.mkdtemp(prefix=f'live-update-{args.block_size}-',dir=base))
    if args.root_image is None:
        args.root_image=extract_live_payload(args.iso,'root.img',work/'expected-root.img')
    if args.kernel is None:
        args.kernel=extract_live_payload(args.iso,'kunix-kodiak.elf',work/'expected-kernel.elf')
    image=work/'nvme.img'
    subprocess.run(['cp','--sparse=always',str(args.image),str(image)],check=True)
    block=args.block_size
    with image.open('rb') as disk:
        disk.seek(block)
        header=disk.read(block)
        if header[:8]!=b'EFI PART':
            raise RuntimeError('source has no expected GPT')
        entries=struct.unpack_from('<Q',header,72)[0]
        disk.seek(entries*block)
        table=disk.read(128*3)
    ranges=[]
    for i in range(3):
        first,last=struct.unpack_from('<QQ',table,i*128+32)
        if not first or last<first:
            raise RuntimeError('source lacks expected EFI/root/home partition')
        ranges.append((first*block,(last-first+1)*block))
    original_data=[digest_range(image,*region) for region in ranges[1:]]
    gpt_before=digest_range(image,0,34*block)
    esp=f'{image}@@{ranges[0][0]}'
    config=subprocess.check_output(['mtype','-i',esp,'::limine.conf'])
    if b'KUPDATE' in config:
        raise RuntimeError('source already enrolled; requires original installation fixture')

    def boot(label,command,live):
        cmd=[sys.executable,str(ROOT/'buildutils/test.py'),'--no-ata','--machine','q35','--mem','512M',
             '--nvme',str(image),'--nvme-block-size',str(block),'--timeout','900',
             '--command',command,'--log',str(work/f'{label}-serial.log'),
             '--serial-live',str(work/f'{label}-live.log')]
        if live:
            cmd+=['--iso',str(args.iso)]
        else:
            cmd+=['--boot-nvme','--firmware','/usr/share/OVMF/OVMF_CODE_4M.fd',
                  '--firmware-vars','/usr/share/OVMF/OVMF_VARS_4M.fd']
        with (work/f'{label}.log').open('w') as log:
            result=subprocess.run(cmd,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        print((work/f'{label}.log').read_text(),flush=True)
        if result.returncode:
            raise RuntimeError(f'{label} failed; logs: {work}')

    boot('bootstrap','liveupdatetest stage',True)
    if [digest_range(image,*region) for region in ranges[1:]]!=original_data:
        raise RuntimeError('bootstrap changed original root or home partition bytes')
    if digest_range(image,0,34*block)!=gpt_before:
        raise RuntimeError('bootstrap changed GPT/partition layout')
    print('PASS live bootstrap: original root/home partition SHA-256 and GPT unchanged',flush=True)
    config=subprocess.check_output(['mtype','-i',esp,'::limine.conf'])
    if b'kernel_path: boot():/KUPDATEA/KERNEL.ELF' not in config or b'/Kunix original installation' not in config:
        raise RuntimeError('missing selected generation or original fallback boot entry')
    for name,expected in [('KERNEL.ELF',args.kernel),('root.img',args.root_image)]:
        actual=work/name
        subprocess.run(['mcopy','-o','-i',esp,f'::/KUPDATEA/{name}',str(actual)],check=True)
        length=expected.stat().st_size
        if digest_range(actual,0,length)!=digest_range(expected,0,length):
            raise RuntimeError(f'live bootstrap staged the wrong {name}')
    print('PASS live bootstrap: selected payload hashes match pristine build inputs',flush=True)
    boot('selected-generation','liveupdatetest verify',False)
    print(f'PASS real NVMe {block} live bootstrap without network/reinstall; logs: {work}',flush=True)


if __name__=='__main__':
    try:
        main()
    except (OSError,RuntimeError,subprocess.SubprocessError) as error:
        sys.exit(f'Live bootstrap test failed: {error}')
