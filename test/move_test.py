import os
import random
import shutil
import sys

mountpoint = sys.argv[1]

filename = "cosfs_test_%d" % random.randint(0, 1000000)
origin_file = mountpoint + filename

data = "hello"
# suppose the "multipart_size" is set to 1 MB.
data = '0' * (2 * 1024 * 1024 + 12)
f = open(origin_file, 'w')
f.write(data)
f.flush()

origin_files = os.listdir(mountpoint)
dest_path = os.getcwd() + "/"

for g in origin_files:
    shutil.move(mountpoint + g, dest_path)

    sz = os.stat(dest_path + g).st_size
    os.remove(dest_path + g)
    assert sz == len(data), "%d, %d"%(sz, len(data))


data  = '0' * (5 * 1024 * 1024 * 1024 + 12)
f = open(origin_file, 'w')
f.write(data)
f.flush()
for g in origin_files:
    shutil.move(mountpoint + g, dest_path)

    sz = os.stat(dest_path + g).st_size
    os.remove(dest_path + g)
    assert sz == len(data), "%d, %d"%(sz, len(data))