from qcloud_cos import CosConfig
from qcloud_cos import CosS3Client
import sys
import logging
import hashlib
import crcmod
import os

logging.basicConfig(level=logging.ERROR, stream=sys.stdout)

cos_client = None


def init_cos_client(region, bucket_name):
    global cos_client
    secret_id = ''
    secret_key = ''
    with open('/etc/passwd-cosfs') as fl:
        for line in fl.readlines():
            if bucket_name in line:
                parts = line.split(":")
                if len(parts) < 3:
                    raise ValueError("unexpected secret info in /etc/passwd-cosfs")
                secret_id = parts[1]
                secret_key = parts[2]
    if len(region) == 0 or len(secret_id) == 0 or len(secret_key) == 0:
        raise ValueError("Unexpected region or secret_id or secret_key")
    config = CosConfig(Region=region, SecretId=secret_id, SecretKey=secret_key, Token=None, Scheme='http')
    cos_client = CosS3Client(config)


def get_cos_file_crc64(bucket, key):
    response = cos_client.get_object(Bucket=bucket, Key=key)
    c64 = crcmod.mkCrcFun(0x142F0E1EBA9EA3693, initCrc=0, xorOut=0xffffffffffffffff, rev=True)
    body = response['Body'].read(1024*1024*100)
    return str(c64(body))


def verify_file_checksum_length_with_local(bucket, key, local_file_path):
    object_attr = cos_client.head_object(Bucket=bucket, Key=key)
    c64 = crcmod.mkCrcFun(0x142F0E1EBA9EA3693, initCrc=0, xorOut=0xffffffffffffffff, rev=True)
    cos_len = int(object_attr['Content-Length'])
    cos_crc64 = object_attr['x-cos-hash-crc64ecma']
    local_len = os.stat(local_file_path).st_size
    local_crc64 =str(c64(open(local_file_path, 'rb').read()))
    if local_len > 0 and cos_crc64 == '':
        cos_crc64 = get_cos_file_crc64(bucket, key)
        print('key' + key + " cos crc64:" + cos_crc64)
    if cos_len != local_len or (cos_len != 0 and cos_crc64 != local_crc64):
        print(object_attr)
        raise ValueError('file diff, local file:' + local_file_path + ', len:' + str(local_len) +
                         ', crc64:' + local_crc64 + '; cos file:' + key + ', len:' + str(cos_len) +
                         ', crc64:' + cos_crc64)


def verify_file_checksum_length(bucket, key, local_len, local_crc64):
    object_attr = cos_client.head_object(Bucket=bucket, Key=key)
    cos_len = int(object_attr['Content-Length'])
    cos_crc64 = object_attr['x-cos-hash-crc64ecma']
    if local_len > 0 and cos_crc64 == '':
        cos_crc64 = get_cos_file_crc64(bucket, key)
        print('key' + key + " cos crc64:" + cos_crc64)
    if cos_len != local_len or (cos_len != 0 and cos_crc64 != local_crc64):
        raise ValueError('file diff, local len:' + str(local_len) +
                         ', crc64:' + local_crc64 + '; cos file:' + key + ', len:' + str(cos_len) +
                         ', crc64:' + cos_crc64)