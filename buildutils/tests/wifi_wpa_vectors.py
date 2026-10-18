# This file has been generated in whole or in part by a Large Language Model
# Independent authenticator fixtures: Python hashlib/hmac and OpenSSL RFC3394.
import hashlib,hmac,struct,ctypes,ctypes.util
pmk=hashlib.pbkdf2_hmac('sha1',b'password',b'IEEE',4096,32)
ap=bytes.fromhex('001a2b3c4d5e'); sta=bytes.fromhex('021122334455')
an=bytes.fromhex('11223344')+bytes(range(4,32)); sn=bytes(range(0x5a,0x7a))
data=min(ap,sta)+max(ap,sta)+min(an,sn)+max(an,sn)
ptk=b''.join(hmac.digest(pmk,b'Pairwise key expansion\0'+data+bytes([i]),'sha1') for i in range(4))[:64]
rsn=bytes.fromhex('30140100000fac040100000fac040100000fac020000')
gtk=bytes.fromhex('aabbccddeeff00112233445566778899'); gtk2=bytes(range(16))
def wrap(data):
    if len(data)%8: data+=b'\xdd'+bytes((7-len(data))%8)
    lib=ctypes.CDLL(ctypes.util.find_library('crypto'))
    schedule=ctypes.create_string_buffer(256)
    assert lib.AES_set_encrypt_key(ptk[16:32],128,schedule)==0
    out=ctypes.create_string_buffer(len(data)+8)
    assert lib.AES_wrap_key(schedule,None,out,data,len(data))==len(data)+8
    return out.raw
def kde(key,idx): return b'\xdd\x16\x00\x0f\xac\x01'+bytes([idx,0])+key
def frame(info,replay,nonce=an,data=b'',keylen=16):
    p=bytearray(bytes([2,3])+struct.pack('>H',95+len(data))+b'\x02'+struct.pack('>HHQ',info,keylen,replay)+nonce+bytes(48)+struct.pack('>H',len(data))+data)
    if info&256:p[81:97]=hmac.digest(ptk[:16],p,'sha1')[:16]
    return p
vectors=dict(same_key_m3=frame(0x13ca,7,data=wrap(rsn+kde(gtk,2))),pmk=pmk,ptk=ptk,rsn=rsn,gtk=gtk,gtk2=gtk2,
 m1=frame(0x008a,1),m1_retry=frame(0x008a,2),m2_retry=frame(0x010a,2,sn,rsn,0),m2=frame(0x010a,1,sn,rsn,0),
 m3=frame(0x13ca,2,data=wrap(rsn+kde(gtk,2))),m4=frame(0x030a,2,bytes(32),keylen=0),
 m3_retry=frame(0x13ca,3,data=wrap(rsn+kde(gtk,2))),m4_retry=frame(0x030a,3,bytes(32),keylen=0),
 wrong_nonce=frame(0x13ca,2,bytes(32),wrap(rsn+kde(gtk,2))),
 bad_kde=frame(0x13ca,2,data=wrap(rsn+b'\xdd\x16\x00\x0f\xac\x01\x02\x00'+gtk[:8])),
 rsn_mismatch=frame(0x13ca,2,data=wrap(rsn[:-2]+b'\x01\x00'+kde(gtk,2))),
 no_rsn=frame(0x13ca,2,data=wrap(kde(gtk,2))),
 group=frame(0x1382,4,bytes(32),wrap(kde(gtk2,1))),group_ack=frame(0x0302,4,bytes(32),keylen=0),
 group_retry=frame(0x1382,5,bytes(32),wrap(kde(gtk2,1))))
# A subsequent AP pairwise rekey uses a fresh station nonce and replay epoch.
sn=bytes(range(0x5b,0x7b))
data=min(ap,sta)+max(ap,sta)+min(an,sn)+max(an,sn)
ptk=b''.join(hmac.digest(pmk,b'Pairwise key expansion\0'+data+bytes([i]),'sha1') for i in range(4))[:64]
vectors.update(rekey_ptk=ptk,rekey_m1=frame(0x008a,6),
 rekey_m2=frame(0x010a,6,sn,rsn,0),
 rekey_m3=frame(0x13ca,7,data=wrap(rsn+kde(gtk2,1))),
 rekey_m4=frame(0x030a,7,bytes(32),keylen=0))
with open('buildutils/tests/wifi_wpa_vectors.h','w') as f:
 f.write('// This file has been generated in whole or in part by a Large Language Model\n// Generated independently with Python hashlib/hmac and OpenSSL AES-wrap.\n')
 for name,value in vectors.items():
  f.write('static const u8 v_'+name+'[] = {'+','.join('0x%02x'%x for x in value)+'};\n')
