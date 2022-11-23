# -*- coding: UTF-8 -*-

import os
import sys
import random
import string


MOUNT_POINT = sys.argv[1]
BUCKET_REGION = sys.argv[2]
BUCKET_NAME = sys.argv[3]
BUCKET_PATH = ''
if len(sys.argv) >= 5:
    BUCKET_PATH = sys.argv[4]

TEST_CONTENT = "Hello World, This is a test content"
NEW_CONTENT = "This is a new content for test"
FILE_NAME = "test.txt"
ALT_FILE_NAME = "alt-test.txt"
FILE_PATH = os.path.join(MOUNT_POINT, FILE_NAME)
ALT_FILE_PATH = os.path.join(MOUNT_POINT, ALT_FILE_NAME)

KB = 1024
MB = 1024 * 1024

RANDOM_BUFFER = ''.join([random.choice(string.ascii_letters + string.digits) for i in range(5 * MB)]) * 10