#ifndef COS_DIR_H
#define COS_DIR_H
#include <string>
#include "common.h"
#include "cos_log.h"
#include "cosfs_util.h"
using std::string;

class CosDir {
public:
    static int cosfs_opendir(const char* path, struct fuse_file_info* fi);
    static int cosfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi);
};

#endif
