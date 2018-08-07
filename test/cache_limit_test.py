# -*- coding: utf-8 -*-
#test for cache
#test cp mv
#test use cache_dir/cache_limit
import os
import time
import shutil
import filecmp
import sys
import random
import string

class CacheLimitTest:
    appid           = ""
    bucket_name     = ""
    bucket_place    = ""
    mnt_dir         = ""
    cache_dir       = ""
    cache_data_dir  = ""
    cache_stat_dir  = ""
    cache_limit     = -1

    def __init__(self, appid, bucket_name, bucket_place, mnt_dir, cache_dir):
        self.appid = appid
        self.bucket_name = bucket_name
        self.bucket_place = bucket_place
        self.mnt_dir = mnt_dir
        self.cache_dir = cache_dir
        self.cache_data_dir = os.path.join(cache_dir, bucket_name)
        self.cache_stat_dir = os.path.join(self.cache_dir, "." +  bucket_name + ".stat")

    def set_cache_limit(self, cache_limit):
        self.cache_limit = cache_limit

    def mount_file_system(self, is_rm_files=True, rm_file_pattern="*"):
        mnt_command = " ".join(["sudo cosfs",
                                self.bucket_name + "-" + self.appid,
                                self.mnt_dir,
                                "-ourl=" + self.bucket_place,
                                "-odbglevel=dbg",
                                "-oallow_other",
                                "-ocurldbg",
                                "-ouse_cache=" + self.cache_dir
                                ])
        if 0 < self.cache_limit:
            mnt_command = " ".join([mnt_command, "-ocache_limit_size=" + str(self.cache_limit)])

        try:
            os.mkdir(self.cache_dir)
        except OSError as exc:
            if is_rm_files:
                os.system("rm -rf " + os.path.join(self.cache_data_dir, rm_file_pattern))
                os.system("rm -rf " + os.path.join(self.cache_stat_dir,rm_file_pattern))

        try:
            os.mkdir(self.mnt_dir)
        except OSError as exc:
            if is_rm_files:
                os.system("rm -rf " + os.path.join(self.mnt_dir, rm_file_pattern))
        print("mount command: %s" % mnt_command)
        os.system("sudo umount -l " + self.mnt_dir)
        os.system(mnt_command)


    def check_mnt_data(self, org_file_path):
        mnt_path = os.path.join(self.mnt_dir, org_file_path)
        if os.path.isfile(org_file_path) and os.path.isfile(mnt_path):
            return filecmp.cmp(org_file_path, mnt_path)
        return False

    def check_cache_data(self, org_file_path):
        cache_path = os.path.join(self.cache_data_dir, org_file_path)
        if os.path.isfile(org_file_path) and os.path.isfile(cache_path):
            return filecmp.cmp(org_file_path, cache_path)
        return False

    def check_meta_exist(self, org_file_path):
        return os.path.isfile(os.path.join(self.cache_stat_dir, org_file_path))

    def cache_limit_sleep(self):
        if 0 < self.cache_limit:
            time.sleep(5)

    def check_cache_dir(self, test_name, file_infos):
        """"
        check mnt data, cache data , .stat  if correct
        """
        for file in file_infos:
            if self.check_mnt_data(file[0]) and ((self.check_cache_data(file[0]) + self.check_meta_exist(file[0]))\
                    != file[2] * 2):
                print("%s not passed: %s cache stat is wrong "%(test_name, file[0]))
                exit(-1)
        print(test_name + " passed")

    def generate_file(self, name, file_size):
        with open(name, 'w') as file:
            file.write(''.join([random.choice(string.digits + string.ascii_letters) for i in range(file_size * 1024)]))

    def mv_cp_template(self, cmd_name, file_infos):
        """"
        mv or cp data to mount point
        """
        for file in file_infos:
            self.generate_file(file[0], file[1])
            shutil.copy(file[0], file[0] + "s")
            cmd = " ".join([cmd_name, file[0], os.path.join(self.mnt_dir, file[0])])
            print(cmd)
            os.system(cmd)
            shutil.move(file[0] + "s", file[0])
            self.cache_limit_sleep()

    def rm_files(self, file_infos):
        for file in file_infos:
            mnt_path = os.path.join(self.mnt_dir, file[0])
            if os.path.exists(mnt_path):
                cache_path = os.path.join(self.cache_data_dir, file[0])
                stat_path = os.path.join(self.cache_stat_dir, file[0])
                os.remove(mnt_path)
                if os.path.exists(mnt_path):
                    print("mnt_path: %s still exist" % mnt_path)
                    exit(-1)

                if os.path.exists(cache_path):
                    print("cache_path: %s still exist" % cache_path)
                    exit(-1)

                if os.path.exists(stat_path):
                    print("cache_path: %s still exist" % stat_path)
                    exit(-1)
        self.cache_limit_sleep()

    def change_expect(self, file_infos):
        if self.cache_limit < 0:
            for idx in range(len(file_infos)):
                file_infos[idx][2] = True
        return file_infos

    def basic_test(self):
        """
        generate a 5M , 1 k file
        mv or cp it to mount file system
        """
        #[filename, size, whether should cache]
        file_infos = [['fileA', 5*1024, True], ['fileB', 1, False]]
        file_infos = self.change_expect(file_infos)

        #test mv command
        self.rm_files(file_infos)
        self.mv_cp_template('mv', file_infos)
        self.check_cache_dir('basic_test(mv)', file_infos)

        #test cp command
        self.rm_files(file_infos)
        self.mv_cp_template('cp', file_infos)
        self.check_cache_dir('basic_test(cp)', file_infos)

    def increment_test(self):
        """
        generate a 1 2 3 2 1 M file
        mv or cp it  to mount file system
        """
        file_infos = [['fileA', 1024, True], ['fileB', 2 * 1024, True], ['fileC', 3 * 1024, True],
                      ['fileD', 2 * 1024, False], ['fileE', 1 * 1024, False]]
        file_infos = self.change_expect(file_infos)

        #test mv command
        self.rm_files(file_infos)
        self.mv_cp_template("mv", file_infos)
        self.check_cache_dir('increment_test(mv)', file_infos)

        #test cp command
        self.rm_files(file_infos)
        self.mv_cp_template("cp", file_infos)
        self.check_cache_dir('increment_test(cp)', file_infos)

    def test_main(self):
        self.mount_file_system()
        self.basic_test()
        self.increment_test()

if __name__ == "__main__":
    if len(sys.argv) < 5:
        print('Usage: python %s appid bucket_name bucket_place mnt_dir cache_dir' % sys.argv[0])
        sys.exit(-1)
    appid = sys.argv[1]
    bucket_name = sys.argv[2]
    bucket_place = sys.argv[3]
    mnt_dir = sys.argv[4]
    cache_dir = sys.argv[5]

    # appid = "1257125707"
    # bucket_name = "learning"
    # bucket_place = "http://cos.ap-guangzhou.myqcloud.com"
    # mnt_dir = "/mnt/cosfs-remote"
    # cache_dir = "/mnt/cosfs-cache"

    cache_test_dir = 'cache-test'
    try:
        os.mkdir(cache_test_dir)
    except OSError as exc:
        pass
    os.chdir(cache_test_dir)

    print("**********************use cache_limit_size**********************")
    test = CacheLimitTest(appid=appid, bucket_name=bucket_name, bucket_place=bucket_place, mnt_dir=mnt_dir, cache_dir=cache_dir)
    test.set_cache_limit(5)
    test.test_main()

    print("********************not use cache_limit_size**********************")
    test.set_cache_limit(-1)
    test.test_main()
    shutil.rmtree(os.path.join('..', cache_test_dir))
