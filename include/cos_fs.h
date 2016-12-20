#ifndef COS_FS_H
#define COS_FS_H
#include <string>
using std::string;

class CosFS {
public:
    static int cosfs_rmdir(const char* path);
    static int cosfs_symlink(const char* from, const char* to);
    static int cosfs_rename(const char* from, const char* to);
    static int cosfs_link(const char* from, const char* to);
    static void* cosfs_init(struct fuse_conn_info *conn);
    static void cosfs_destroy(void *ptid);
};

#endif


