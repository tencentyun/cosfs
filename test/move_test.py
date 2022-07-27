import os
import shutil
import sys

origin_path = sys.argv[1]
dest_path  = sys.argv[2]

origin_files = os.listdir(origin_path)

for g in origin_files:
    shutil.move(origin_path + g, dest_path)