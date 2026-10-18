#!/usr/bin/env python3
# This file has been generated in whole or in part by a Large Language Model
"""Verify Kunix networking against localhost TCP/UDP/HTTP services in QEMU."""
from contextlib import ExitStack
import http.server
from pathlib import Path
import socketserver
import subprocess
import sys
import threading

ROOT=Path(__file__).resolve().parent.parent
class TCP(socketserver.BaseRequestHandler):
    def handle(self):
        while True:
            data=self.request.recv(8192)
            if not data:return
            self.request.sendall(data)
class UDP(socketserver.BaseRequestHandler):
    def handle(self):
        data,sock=self.request
        sock.sendto(data,self.client_address)
class HTTP(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body=b'kunix-http-ok\n' if self.path=='/test' else b'not found\n'
        self.send_response(200 if self.path=='/test' else 404)
        self.send_header('Content-Length',str(len(body)))
        self.end_headers();self.wfile.write(body)
    def log_message(self,*_args):pass
class TCPServer(socketserver.ThreadingTCPServer):
    allow_reuse_address=True
    daemon_threads=True
class UDPServer(socketserver.ThreadingUDPServer):
    allow_reuse_address=True
    daemon_threads=True

def main():
    work=ROOT/'build/phase4/network-test';work.mkdir(parents=True,exist_ok=True)
    with ExitStack() as stack:
        servers=[TCPServer(('127.0.0.1',18080),TCP),UDPServer(('127.0.0.1',18081),UDP),http.server.ThreadingHTTPServer(('127.0.0.1',18083),HTTP)]
        for server in servers:
            stack.callback(server.server_close)
            threading.Thread(target=server.serve_forever,daemon=True).start()
            stack.callback(server.shutdown)
        for label,command in [('linux','nettest external'),('native','netnative')]:
            cmd=[sys.executable,str(ROOT/'buildutils/test.py'),'--network','--iso',str(ROOT/'build/binaries/kunix-live.iso'),'--no-ata','--mem','512M','--timeout','120','--command',command,'--log',str(work/f'{label}-serial.log')]
            result=subprocess.run(cmd,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
            (work/f'{label}.log').write_text(result.stdout)
            print(result.stdout,flush=True)
            if result.returncode:raise SystemExit(f'FAIL {label}; see {work}')
    print('PASS IPv4 loopback, external TCP/UDP, DNS, HTTP and native Kunix networking')
if __name__=='__main__':main()
