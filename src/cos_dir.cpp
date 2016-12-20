#include <errno.h>
#include "common.h"
#include "cos_dir.h"
#include "cache.h"
#include "cos_info.h"
#include "cos_log.h"
#include "string_util.h"
#include "cos-cpp-sdk/CosApi.h"
#include <sys/time.h>

int CosDir::cosfs_opendir(const char* path, struct fuse_file_info* fi)
{
    S3FS_PRN_INFO("cosfs_opendir,path = %s, mount_bucket_folder=%s",path,CosInfo::mount_bucket_folder.c_str());    
    return 0;
}

int CosDir::cosfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi)
{
    string cospath(CosInfo::mount_bucket_folder + path);
    S3FS_PRN_INFO("cosfs_readdir,path = %s",cospath.c_str());
    std::string::reverse_iterator iter= cospath.rbegin();
    if (iter != cospath.rend() && *iter != '/'){
        cospath.append("/");
    }

    struct stat filestat;
    filestat.st_mode = S_IFREG | 0444;
    struct stat dirstat;
    dirstat.st_nlink = 1;
    dirstat.st_mode = S_IFDIR | 0555;
    string fullpath = "";
    string context = "";
    string result = "";
    bool listover = false;
    uint64_t dircount = 0;
    uint64_t filecount = 0;
    headers_t headers_stat;
    keys_maps stats;
    StatCache* pCache = StatCache::getStatCacheData();
    int  loopcount = 0; //对循环做一个保护,防止死循环
    FolderListReq folderListReq(bucket, cospath, 1000, true, context);;
    while(listover == false && loopcount <= 100) {
        loopcount++;
        long long time_start_flag, time_end_flag;
        timeval tv1;
        gettimeofday(&tv1, NULL);
        time_start_flag = tv1.tv_sec * 1000 * 1000 + tv1.tv_usec;
        result = cos_client->FolderList(folderListReq);
        gettimeofday(&tv1, NULL);
        time_end_flag = tv1.tv_sec * 1000 * 1000 + tv1.tv_usec;
        S3FS_PRN_DBG("listFolder loop[%d] used time=%lld\n",loopcount, time_end_flag - time_start_flag);
        Json::Value ret = StringUtils::StringToJson(result);
        //解析json失败或者返回的响应消息不符合cos格式
        if (!ret.isMember("code"))
        {
            S3FS_PRN_ERR("cosfs_readdir: ListFolder(%s) context(%s) error, return:%s ",cospath.c_str(), context.c_str(), result.c_str());
            return -EINVAL;
        }

        if (ret["code"] != 0)
        {
            S3FS_PRN_ERR("cosfs_readdir: ListFolder(%s),context(%s) error, return:%s ",cospath.c_str(), context.c_str(), result.c_str());
            return -ENOENT;
        }

        Json::Value& data = ret["data"];
        context = data["context"].asString();
        listover = data["listover"].asBool();
        if (listover == false) {
            folderListReq.setContext(context);
        }

        Json::Value& infos = data["infos"];
        for (unsigned int i=0; i < infos.size(); i++)
        {
            string name = infos[i]["name"].asString();
            string mtime = infos[i]["mtime"].asString();
            if (infos[i].isMember("filesize")){
                filestat.st_size = infos[i]["filesize"].asInt64();
                filestat.st_mtime = get_mtime(mtime.c_str());
                filler(buf, name.c_str(), &filestat ,0); //文件
                headers_stat[CONTENT_TYPE] = "file";
                headers_stat[CONTENT_LENGTH] = StringUtils::Uint64ToString(filestat.st_size);
                filecount++;
            } else {
                //对于文件夹,name是以/结尾的,需要删除/
                StringUtils::TrimTailChar(name, '/');
                dirstat.st_mtime = get_mtime(mtime.c_str());
                filler(buf, name.c_str(), &dirstat ,0);  //文件夹
                headers_stat[CONTENT_TYPE] = "dir";
                dircount++;
            } 

            std::string::reverse_iterator iter= cospath.rbegin();
            if (iter != cospath.rend() && *iter == '/'){
                fullpath = cospath + name;
            } else {
                fullpath = cospath + "/" + name;
            }
      
            headers_stat[FILE_NAME] = fullpath;
            headers_stat[LAST_MODIFIED] = mtime;
            stats[fullpath] = headers_stat;
            headers_stat.clear();
            S3FS_PRN_DBG("cosfs_readdir: cache file=%s,size=%s,mtime=%s,type=%s",fullpath.c_str(),headers_stat[CONTENT_LENGTH].c_str(),headers_stat[LAST_MODIFIED].c_str(), headers_stat[CONTENT_TYPE].c_str());
        }

        if (stats.size() > 0 ){
            pCache->AddStatS(stats);
            stats.clear();
        }
    }

    S3FS_PRN_INFO("cosfs_readdir: return to fuse total_count=%lu, filecount=%lu,dircount=%lu", filecount+dircount, filecount, dircount);
    return 0;
}


