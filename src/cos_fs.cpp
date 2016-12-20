#include "common.h"
#include "cos_fs.h"
#include "cos_log.h"
#include "cache_monitor.h"

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

extern pthread_t ntid;
void* CosFS::cosfs_init(struct fuse_conn_info *conn)
{
    //监控线程扫描cache(fuse加载后会kill自定义的线程)
    int ret;
    ret = pthread_create(&ntid, NULL, &CacheMonitor::monitor_run, NULL);
    if (ret != 0)
    {
        S3FS_PRN_EXIT("monitorCache thread create failed, exit, ret=%d",ret);
        exit(EXIT_FAILURE);
    }

    S3FS_PRN_INFO("cosfs_init: success");
    return (void*)&ntid;
}

void CosFS::cosfs_destroy(void *ptid)
{
    //监控线程扫描cache(fuse加载后会kill自定义的线程)
    if (!ptid) {
        return;
    }

    pthread_t* tid = (pthread_t*)ptid;
    if (*tid <= 0) {
        S3FS_PRN_ERR("cosfs_destroy,thread monitorCache tid=%d",*tid);
        return;
    }
    int ret=pthread_cancel(*tid);
    if (ret != 0)
    {
        S3FS_PRN_ERR("cosfs_destroy,cancel thread monitorCache fail, ret=%d",ret);
        return;
    }
    S3FS_PRN_INFO("cosfs_destroy: success");
    return;        
}
