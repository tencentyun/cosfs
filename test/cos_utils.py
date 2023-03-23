from qcloud_cos import CosConfig
from qcloud_cos import CosS3Client
import sys
import logging
import crcmod
import os
import subprocess

logging.basicConfig(level=logging.ERROR, stream=sys.stdout)

cos_client = None
run_dir = ""


def init_cos_client(region, bucket_name, mnt_point):
    global cos_client
    global run_dir
    secret_id = ''
    secret_key = ''
    with open('/etc/passwd-cosfs') as fl:
        for line in fl.readlines():
            if bucket_name in line:
                parts = line.split(":")
                if len(parts) < 3:
                    raise ValueError("unexpected secret info in /etc/passwd-cosfs")
                secret_id = parts[1].strip()
                secret_key = parts[2].strip()
    if len(region) == 0 or len(secret_id) == 0 or len(secret_key) == 0:
        raise ValueError("Unexpected region or secret_id or secret_key")
    print("init cos client")
    config = CosConfig(Region=region, SecretId=secret_id, SecretKey=secret_key, Token=None, Scheme='http')
    cos_client = CosS3Client(config)
    run_dir = os.path.dirname(os.path.normpath(mnt_point))
    print("run_dir:" + run_dir)


def get_cos_file_crc64(bucket, key):
    response = cos_client.get_object(Bucket=bucket, Key=key)
    c64 = crcmod.mkCrcFun(0x142F0E1EBA9EA3693, initCrc=0, xorOut=0xffffffffffffffff, rev=True)
    body = response['Body'].read(1024*1024*100)
    return str(c64(body))


def get_local_file_crc64(file_path):
    result = subprocess.run([os.path.join(run_dir, "coscli-linux"), '-c',
                             os.path.join(run_dir, 'test/cos.yaml'), 'hash', file_path],
                            stdout=subprocess.PIPE)
    return str(result.stdout).split(':')[-1].strip("'n\\ ")


def verify_file_checksum_length_with_local(bucket, key, local_file_path):
    object_attr = cos_client.head_object(Bucket=bucket, Key=key)
    test_crc64 = crcmod.Crc(0x142F0E1EBA9EA3693, initCrc=0, xorOut=0xffffffffffffffff, rev=True)
    # c64 = crcmod.mkCrcFun(0x142F0E1EBA9EA3693, initCrc=0, xorOut=0xffffffffffffffff, rev=True)
    cos_len = int(object_attr['Content-Length'])
    cos_crc64 = object_attr['x-cos-hash-crc64ecma']
    local_len = os.stat(local_file_path).st_size
    # read_bytes = 0
    # buffer_size = 128 * 1024
    # with open(local_file_path, 'rb') as fl:
    #     byte_array = fl.read(buffer_size)
    #     read_bytes = read_bytes + len(byte_array)
    #     while len(byte_array) > 0:
    #         test_crc64.update(byte_array)
    #         byte_array = fl.read(buffer_size)
    #         read_bytes = read_bytes + len(byte_array)
    #         if read_bytes % (100 * 1024 * 1024) == 0:
    #             print('read bytes ' + str(read_bytes))
    local_crc64 = get_local_file_crc64(local_file_path)
    # local_crc64 = str(int(test_crc64.hexdigest(), 16))
    # local_crc64 = str(c64(open(local_file_path, 'rb').read()))
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
