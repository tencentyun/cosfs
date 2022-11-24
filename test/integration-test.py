# -*- coding: UTF-8 -*-

import random
import stat
import string
import filecmp
from cos_utils import *


MOUNT_POINT = sys.argv[1]
BUCKET_REGION = sys.argv[2]
BUCKET_NAME = sys.argv[3]
BUCKET_PATH = ''

TEST_CONTENT = "Hello World, This is a test content"
NEW_CONTENT = "This is a new content for test"
FILE_NAME = "test.txt"
ALT_FILE_NAME = "alt-test.txt"
FILE_PATH = os.path.join(MOUNT_POINT, FILE_NAME)
ALT_FILE_PATH = os.path.join(MOUNT_POINT, ALT_FILE_NAME)

KB = 1024
MB = 1024 * 1024

RANDOM_BUFFER = ''.join([random.choice(string.ascii_letters + string.digits) for i in range(1 * MB)]) * (1024 + 10)


def read_content(filepath):
    with open(filepath, 'r') as fl:
        data = fl.read()
        return data


def write_content(filepath, content):
    with open(filepath, 'w') as fl:
        fl.write(content)


def remove_file(filepath):
    os.remove(filepath)
    if os.path.exists(filepath):
        raise ValueError("failed to remove file " + filepath)


def remove_folder(folder_path):
    os.rmdir(folder_path)
    if os.path.exists(folder_path):
        raise ValueError("failed to remove folder " + folder_path)


def get_chars(size):
    radix = random.randint(0, 1 * MB)
    if (size + 1 * MB) < len(RANDOM_BUFFER):
        return RANDOM_BUFFER[radix:radix + size]
    raise ValueError("size" + str(size) + " is too large")


def get_cos_path(mnt_file_path):
    cos_file_path = mnt_file_path[len(MOUNT_POINT):]
    if len(BUCKET_PATH) > 0:
        cos_file_path = os.path.join(BUCKET_PATH, mnt_file_path[len(MOUNT_POINT):])
    return cos_file_path


def check_file_equal(local_file_path, mnt_file_path):
    print("check detail:" + local_file_path + ",size:" + str(os.stat(local_file_path).st_size) + ", " + mnt_file_path +
          " size:" + str(os.stat(mnt_file_path).st_size))
    status = filecmp.cmp(local_file_path, mnt_file_path, False)
    if not status:
        raise ValueError(local_file_path + " diff to " + mnt_file_path)
    cos_file_path = get_cos_path(mnt_file_path)
    print('remote check:' + local_file_path + ', cos_file_path:' + cos_file_path)
    verify_file_checksum_length_with_local(BUCKET_NAME, cos_file_path, local_file_path)


def cal_crc64(content):
    c64 = crcmod.mkCrcFun(0x142F0E1EBA9EA3693, initCrc=0, xorOut=0xffffffffffffffff, rev=True)
    return str(c64(content.encode()))


def test_check_file_equal():
    print("Testing check file equal")
    tmp_file = "/tmp/test.txt"
    with open(tmp_file, 'w') as fl:
        fl.write('aaa')
    with open(FILE_PATH, 'w') as fl:
        fl.write('bbbb')
    try:
        check_file_equal(tmp_file, FILE_PATH)
    except:
        remove_file(tmp_file)
        remove_file(FILE_PATH)
        pass
    else:
        raise ValueError(tmp_file + " is expected diff to " + FILE_PATH)


def check_file_equal(local_file_path, mnt_file_path):
    print("check detail:" + local_file_path + "size:" + str(os.stat(local_file_path).st_size) + ", " + mnt_file_path +
          " size:" + str(os.stat(mnt_file_path).st_size))
    status = filecmp.cmp(local_file_path, mnt_file_path, False)
    if not status:
        raise ValueError(local_file_path + " diff to " + mnt_file_path)
    cos_file_path = get_cos_path(mnt_file_path)
    print('remote check:' + local_file_path + ', cos_file_path:' + cos_file_path)
    verify_file_checksum_length_with_local(BUCKET_NAME, cos_file_path, local_file_path)


def test_truncate():
    print("Testing truncate not exist ...")
    tmp_file = "/tmp/test.txt"
    try:
        os.truncate(tmp_file, 10)
    except:
        pass
    try:
        os.truncate(FILE_PATH, 10)
        print("truncate result not expected")
        exit(-1)
    except:
        pass

    print('Testing truncate shorter ...')
    str_array = [get_chars(10), get_chars(1 * KB), get_chars(10 * MB), get_chars(20 * MB)]
    truncate_array = [0, 512, 4 * MB, 5 * MB]
    for idx in range(len(str_array)):
        write_content(tmp_file, str_array[idx])
        write_content(FILE_PATH, str_array[idx])
        os.truncate(tmp_file, truncate_array[idx])
        os.truncate(FILE_PATH, truncate_array[idx])
        check_file_equal(tmp_file, FILE_PATH)
        remove_file(tmp_file)
        remove_file(FILE_PATH)

    print('Testing truncate longer ...')
    str_array = [get_chars(0), get_chars(1 * KB), get_chars(10 * MB), get_chars(20 * MB)]
    truncate_array = [512, 4 * MB, 20 * MB, 100*MB]
    for idx in range(len(str_array)):
        write_content(tmp_file, str_array[idx])
        write_content(FILE_PATH, str_array[idx])
        os.truncate(tmp_file, truncate_array[idx])
        os.truncate(FILE_PATH, truncate_array[idx])
        check_file_equal(tmp_file, FILE_PATH)
        remove_file(tmp_file)
        remove_file(FILE_PATH)


def test_ftruncate():
    print('Testing ftruncate shorter...')
    tmp_file = "/tmp/test.txt"
    str_array = [get_chars(10), get_chars(1 * KB), get_chars(10 * MB), get_chars(20 * MB)]
    truncate_array = [0, 512, 4 * MB, 5 * MB]
    new_data = get_chars(1 * MB)
    for idx in range(len(str_array)):
        fd = os.open(tmp_file, os.O_WRONLY | os.O_TRUNC | os.O_CREAT)
        os.write(fd, str_array[idx].encode())
        os.ftruncate(fd, truncate_array[idx])
        os.write(fd, new_data.encode())
        os.close(fd)
        fd1 = os.open(FILE_PATH, os.O_WRONLY | os.O_TRUNC | os.O_CREAT)
        os.write(fd1, str_array[idx].encode())
        os.ftruncate(fd1, truncate_array[idx])
        os.write(fd, new_data.encode())
        os.close(fd1)
        check_file_equal(tmp_file, FILE_PATH)
        remove_file(tmp_file)
        remove_file(FILE_PATH)

    print('Testing ftruncate longer ...')
    str_array = [get_chars(0), get_chars(1 * KB), get_chars(10 * MB), get_chars(20 * MB)]
    truncate_array = [512, 4 * MB, 20 * MB, 100 * MB]
    for idx in range(len(str_array)):
        fd = os.open(tmp_file, os.O_WRONLY | os.O_TRUNC | os.O_CREAT)
        os.write(fd, str_array[idx].encode())
        os.ftruncate(fd, truncate_array[idx])
        os.write(fd, new_data.encode())
        os.close(fd)
        fd1 = os.open(FILE_PATH, os.O_WRONLY | os.O_TRUNC | os.O_CREAT)
        os.write(fd1, str_array[idx].encode())
        os.ftruncate(fd1, truncate_array[idx])
        os.write(fd, new_data.encode())
        os.close(fd1)
        check_file_equal(tmp_file, FILE_PATH)
        remove_file(tmp_file)
        remove_file(FILE_PATH)


def test_write_and_utimens_chmod_chown():
    print("Testing write and utimens/chmod/chown")
    tmp_file = "/tmp/test.txt"
    str_array = [get_chars(1 * KB), get_chars(1024 * MB)]
    for idx in range(len(str_array)):
        fd = os.open(tmp_file, os.O_WRONLY | os.O_TRUNC | os.O_CREAT)
        for j in range(6):
            os.write(fd, str_array[idx].encode())
        os.utime(tmp_file, (1330712280, 1330712292))
        os.chmod(tmp_file, stat.S_IXGRP)
        os.chown(tmp_file, 0, 0)
        os.write(fd, "data".encode())
        os.close(fd)

        fd1 = os.open(FILE_PATH, os.O_WRONLY | os.O_TRUNC | os.O_CREAT)
        for j in range(6):
            os.write(fd, str_array[idx].encode())
        os.utime(FILE_PATH, (1330712280, 1330712292))
        os.chmod(FILE_PATH, stat.S_IXGRP)
        os.chown(FILE_PATH, 0, 0)
        os.write(fd1, "data".encode())
        os.close(fd1)

        check_file_equal(tmp_file, FILE_PATH)
        remove_file(tmp_file)
        remove_file(FILE_PATH)


init_cos_client(BUCKET_REGION, BUCKET_NAME, MOUNT_POINT)
test_truncate()
test_ftruncate()
test_write_and_utimens_chmod_chown()
