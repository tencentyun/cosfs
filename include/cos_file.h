#ifndef COS_FILE_H
#define COS_FILE_H
#include <string>

using std::string;

class CosFile {
public:
    static int cosfs_open(const char* path, struct fuse_file_info* fi);
    static int cosfs_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi);
    static int cosfs_release(const char* path, struct fuse_file_info* fi);
    static int cosfs_truncate(const char* path, off_t size);
    static int cosfs_statfs(const char* path, struct statvfs* stbuf);
    static int cosfs_getattr(const char *path, struct stat *stbuf);
    static int cosfs_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi);
    static int cosfs_access(const char* path, int mask);
};

#endif

