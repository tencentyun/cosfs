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
};

#endif


