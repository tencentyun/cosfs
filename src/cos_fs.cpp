#include "common.h"
#include "cos_fs.h"
#include "cos_log.h"

int CosFS::cosfs_rmdir(const char* path)
{
    S3FS_PRN_INFO("cosfs_rmdir: path = %s",path);
    return 0;
}
int CosFS::cosfs_symlink(const char* from, const char* to)
{
    S3FS_PRN_INFO("cosfs_symlink: from=%s, to=%s", from ,to);
    return 0;        
}
int CosFS::cosfs_rename(const char* from, const char* to)
{
    S3FS_PRN_INFO("cosfs_symlink: from=%s, to=%s", from ,to);
    return 0;        
}
int CosFS::cosfs_link(const char* from, const char* to)
{
    S3FS_PRN_INFO("cosfs_symlink: from=%s, to=%s", from ,to);
    return 0;        
}

