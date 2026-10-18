# This file has been generated in whole or in part by a Large Language Model
# Independent OpenSSL EVP CCM generator for CCMP test fixtures.
import ctypes as c,ctypes.util
l=c.CDLL(ctypes.util.find_library('crypto'))
l.EVP_CIPHER_CTX_new.restype=c.c_void_p;l.EVP_aes_128_ccm.restype=c.c_void_p
l.EVP_EncryptInit_ex.argtypes=[c.c_void_p,c.c_void_p,c.c_void_p,c.c_void_p,c.c_void_p]
l.EVP_CIPHER_CTX_ctrl.argtypes=[c.c_void_p,c.c_int,c.c_int,c.c_void_p]
l.EVP_EncryptUpdate.argtypes=[c.c_void_p,c.c_void_p,c.POINTER(c.c_int),c.c_void_p,c.c_int]
l.EVP_EncryptFinal_ex.argtypes=[c.c_void_p,c.c_void_p,c.POINTER(c.c_int)]
l.EVP_CIPHER_CTX_free.argtypes=[c.c_void_p]
def ccm(key,nonce,aad,plain):
 ctx=l.EVP_CIPHER_CTX_new();n=c.c_int();out=c.create_string_buffer(len(plain)+16);tag=c.create_string_buffer(8)
 assert l.EVP_EncryptInit_ex(ctx,l.EVP_aes_128_ccm(),None,None,None)==1
 assert l.EVP_CIPHER_CTX_ctrl(ctx,9,len(nonce),None)==1
 assert l.EVP_CIPHER_CTX_ctrl(ctx,17,8,None)==1
 assert l.EVP_EncryptInit_ex(ctx,None,None,key,nonce)==1
 assert l.EVP_EncryptUpdate(ctx,None,c.byref(n),None,len(plain))==1
 assert l.EVP_EncryptUpdate(ctx,None,c.byref(n),aad,len(aad))==1
 assert l.EVP_EncryptUpdate(ctx,out,c.byref(n),plain,len(plain))==1
 result=out.raw[:n.value]
 assert l.EVP_EncryptFinal_ex(ctx,out,c.byref(n))==1
 assert l.EVP_CIPHER_CTX_ctrl(ctx,16,8,tag)==1
 l.EVP_CIPHER_CTX_free(ctx)
 return result+tag.raw
key=bytes(range(0xc0,0xd0));nonce=bytes.fromhex('00001122334455010000000000');aad=bytes(22);plain=b'hello ccmp world'
print(ccm(key,nonce,aad,plain).hex())
print(ccm(key,bytes.fromhex('07001122334455010000000000'),bytes(22)+b'\x07\0',plain).hex())
