#include <errno.h>
#include <sys/time.h>
#include <iostream>
#include <string>

#include "cos-cpp-sdk/CosApi.h"
#include "curl/curl.h"
#include "cache.h"
#include "common.h"
#include "cos_file.h"
#include "cos_info.h"
#include "cos_log.h"
#include "cosfs_util.h"
#include "fdpage.h"
#include "string_util.h"
#include "http_util.h"

using std::string;
using std::cout;
using std::endl;
//using namespace qcloud_cos;
extern CosAPI* cos_client;

int CosFile::cosfs_open(const char* path, struct fuse_file_info* fi)
{
    if (!path || !fi)
    {
        S3FS_PRN_ERR("cosfs_open: path or fi is null");
        return -EBADF;
    }

    S3FS_PRN_INFO("cosfs_open: path=%s, mount_bucket_folder=%s",path,CosInfo::mount_bucket_folder.c_str());
    string cospath(CosInfo::mount_bucket_folder + string(path));

    StatCache* pCache = StatCache::getStatCacheData();
    headers_t stat;
    if (false == pCache->GetStat(cospath,&stat,OPEN_FILE))
    {
        S3FS_PRN_ERR("cosfs_open get stat file from cache fail, error");
        return -ENOENT;
    }

    return 0;
}

int CosFile::cosfs_access(const char* path, int mask)
{
    if (!path)
    {
        S3FS_PRN_ERR("cosfs_access: path is null");
        return -EBADF;
    }    
    S3FS_PRN_INFO("cosfs_access: path=%s, mount_bucket_folder=%s",path,CosInfo::mount_bucket_folder.c_str());

    return 0;
}


int CosFile::cosfs_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
    if (!path)
    {
        S3FS_PRN_ERR("cosfs_write: path is null");
        return -EBADF;
    }     
    S3FS_PRN_INFO("cosfs_write: path=%s, mount_bucket_folder=%s",path,CosInfo::mount_bucket_folder.c_str());
    return size;
}

int CosFile::cosfs_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
    if (!path || !buf)
    {
        S3FS_PRN_ERR("cosfs_read: path or buf null");
        return -EBADF;
    }

    string cospath(CosInfo::mount_bucket_folder+path);

    S3FS_PRN_INFO("cosfs_read: path=%s, offset: %ld , size :%ld", cospath.c_str(), offset, size);
    StatCache* pCache = StatCache::getStatCacheData();
    stat_cache_entry* pStat = pCache->GetStat(cospath);
    if (pStat && pCache->isCacheValid(pStat)) {
        if (offset >= pStat->stbuf.st_size) {
            S3FS_PRN_INFO("cosfs_read: cache file:%s,offset=%lu larger than file size:%lu", cospath.c_str(), offset, pStat->stbuf.st_size);
            return 0;
        }
    }

    PageList* pagelist = FileCacheManager::get()->Open(cospath);
    FileDownloadReq req(CosInfo::bucket, cospath);
    size_t down_len = 0;
    int ret_code = 0;
    if (!pagelist) {
        down_len = cos_client->FileDownload(req, buf, size, offset, &ret_code);
    } else {
        fdpage_list_t list;
        struct timeval start, end;
        gettimeofday(&start, NULL);
        ssize_t cached_size = pagelist->LoadAndOcuppyCachePages(offset, size, &list);
        gettimeofday(&end, NULL);
        S3FS_PRN_INFO("Load from cos cost %d us", (end.tv_sec  - start.tv_sec)* 1000000 + (end.tv_usec - start.tv_usec));
        if (cached_size > 0) {
            S3FS_PRN_INFO("Found cache in pagelist, cached size %ld, offset: %ld, size :%ld", cached_size, offset, size);
            string rspbuf;
            cached_size = pagelist->GetCachedContent(&list, offset, size, &rspbuf);
            pagelist->ReleasePages(&list);
            if (cached_size > 0) {
                size_t len = (size < rspbuf.length())? size : rspbuf.length();
                memcpy(buf,(char*)rspbuf.c_str(), len);
                down_len = len;
            }
        } else {
            S3FS_PRN_ERR("No cached size, download %s from cos directly, offset : %ld, size: %ld", cospath.c_str(), offset, size);
            down_len = cos_client->FileDownload(req, buf, size, offset, &ret_code);
        }

        FileCacheManager::get()->Close(cospath);
    }

    if (ret_code != 0) {
        S3FS_PRN_ERR("cosfs_read: fail , ret_code=%d, rsp: %s", ret_code, buf);
        return -EIO;
    }

    S3FS_PRN_INFO("cosfs_read: success, byte = %d", down_len);    
    return down_len;
}

int CosFile::cosfs_release(const char* path, struct fuse_file_info* fi)
{
    S3FS_PRN_INFO("cosfs_release: path=%s, mount_bucket_folder=%s",path,CosInfo::mount_bucket_folder.c_str());
    string cospath(CosInfo::mount_bucket_folder+path);
    StatCache* pCache = StatCache::getStatCacheData();
    pCache->DelStat(cospath);
    return 0;
}

int CosFile::cosfs_statfs(const char* path, struct statvfs* stbuf)
{
    stbuf->f_bsize  = 0X1000000;
    stbuf->f_blocks = 0X1000000;
    stbuf->f_bfree  = 0x1000000;
    stbuf->f_bavail = 0x1000000;
    stbuf->f_namemax = NAME_MAX;
    return 0;
}

int CosFile::cosfs_getattr(const char *path, struct stat *stbuf)
{
    if (!path || !stbuf){
	    S3FS_PRN_ERR("cosfs_getattr ,path or stbuf is null");
        return -EBADF;
    }
    
    S3FS_PRN_INFO("cosfs_getattr,path = %s,mount_bucket_folder = %s",path,CosInfo::mount_bucket_folder.c_str());
    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0 ||strcmp(path, ".") == 0) {
        stbuf->st_mode = S_IFDIR | 0555;
        stbuf->st_nlink = 1;
        stbuf->st_uid = 0;
        stbuf->st_gid = 0;
        return 0;
    } else  {
        string result;
        Json::Value ret;
        string cospath(CosInfo::mount_bucket_folder+path);

        //S3FS_PRN_INFO("cosfs_getattr, cosPath = %s", cospath.c_str());
        // 百视通特殊需求
#if 1
        if (string(path).find(".") == string::npos) {
            stbuf->st_mode = S_IFDIR | 0555;
            stbuf->st_nlink = 2;
            stbuf->st_uid = 0;
            stbuf->st_gid = 0;
            return 0;
        }
#endif

        //缓存处理
        StatCache* pCache = StatCache::getStatCacheData();
        stat_cache_entry* pStat = pCache->GetStat(cospath);
  
        if (pStat)
        {
            if (pCache->isCacheValid(pStat))
            {
                S3FS_PRN_INFO("cosfs_getattr: cache file:%s,mode:0%o,size:%lu,mtime:%lu,isCacheValid", cospath.c_str(), pStat->stbuf.st_mode, pStat->stbuf.st_size,pStat->stbuf.st_mtime);
                stbuf->st_mode = pStat->stbuf.st_mode;
                stbuf->st_size = pStat->stbuf.st_size;
                stbuf->st_mtime = pStat->stbuf.st_mtime;
                stbuf->st_nlink = pStat->stbuf.st_nlink;
                stbuf->st_uid = pStat->stbuf.st_uid;
                stbuf->st_gid = pStat->stbuf.st_gid; 
                return 0;
            } 
        }

        struct timeval start, end;
        gettimeofday(&start, NULL);
        S3FS_PRN_INFO("cosfs_getattr path = %s", cospath.c_str());
        FileStatReq fileStatReq(bucket,cospath);
        result = cos_client->FileStat(fileStatReq);
        gettimeofday(&end, NULL);
        S3FS_PRN_INFO("stat file from cos cost %d us", (end.tv_sec  - start.tv_sec)* 1000000 + (end.tv_usec - start.tv_usec));
        ret = StringUtils::StringToJson(result);
        //解析json失败或者返回的响应消息不符合cos格式
        if (!ret.isMember("code"))
        {
            S3FS_PRN_ERR("stat file(%s) error, return:%s ",cospath.c_str(),result.c_str());
            return -EACCES;
        }

        bool isdir = false;
        if (ret["code"] != 0)
        {
            S3FS_PRN_ERR("stat file(%s) error, return:%s ",cospath.c_str(), result.c_str());
            FolderStatReq folderStatReq(bucket,cospath);
            result = cos_client->FolderStat(folderStatReq);
            ret = StringUtils::StringToJson(result);
            //解析json失败或者返回的响应消息不符合cos格式
            if (!ret.isMember("code"))
            {
                S3FS_PRN_ERR("stat folder(%s) error, return:%s ",cospath.c_str(),result.c_str());
                return -EACCES;
            }

            if (ret["code"] != 0)
            {
                S3FS_PRN_CRIT("stat folder(%s) error, return:%s ",cospath.c_str(),result.c_str());
                return -ENOENT;
            }

            isdir = true;
        }

        S3FS_PRN_INFO("cosfs_getattr: stat file(%s) success, return:%s ",cospath.c_str(),result.c_str());
        if (ret.isMember("data"))
        {
            Json::Value& data = ret["data"];
            //文件:带sha，目录则不带sha
            if (data.isMember("filesize")) {
                stbuf->st_mode = S_IFREG | 0444;
                stbuf->st_size = data["filesize"].asInt64();
                stbuf->st_nlink = 1;
            } else {
                isdir = true;
                stbuf->st_mode = S_IFDIR | 0555;
                stbuf->st_nlink = 1;
            }

            stbuf->st_mtime = get_mtime((data["mtime"].asString()).c_str());
            stbuf->st_ctime = get_mtime((data["ctime"].asString()).c_str());

            headers_t stat;

            //缓存处理
            if (!isdir){
                stat[CONTENT_LENGTH] = data["filesize"].asString();
                stat[CONTENT_TYPE] = "file"; //content-type带了则为文件
            }

            stat[FILE_NAME] = cospath;
            stat[LAST_MODIFIED] = data["mtime"].asString();

            pCache->AddStat(cospath,stat);

        } else {
            //curl执行超时或者失败都会进入本分支
            S3FS_PRN_ERR("cosfs_getattr: stat file(%s) fail, return:%s ",cospath.c_str(),result.c_str());
            return -EACCES;
        }        
    }

    return 0;
}

int CosFile::cosfs_truncate(const char* path, off_t size)
{
    S3FS_PRN_INFO("cosfs_truncate,path = %s,mount_bucket_folder=%s",path,CosInfo::mount_bucket_folder.c_str());
    return 0;
}

