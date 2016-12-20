#include <pthread.h>
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <dirent.h>
#include <errno.h>
#include <iostream>
#include <sys/types.h>
#include <getopt.h>
#include "cos_fs.h"
#include "cache.h"
#include "string_util.h"
#include "common_util.h"
#include "cos_file.h"
#include "cos-cpp-sdk/CosApi.h"
#include "cos_log.h"
#include "cos_info.h"
#include "cos_dir.h"
#include <signal.h>


using namespace std;
using namespace qcloud_cos;

s3fs_log_level debug_level = S3FS_LOG_ERR;
const char* s3fs_log_nest[S3FS_LOG_NEST_MAX] = {"", "  ", "    ", "      "};
const int nonempty = 0;
string program_name = "";
string bucket = "";
string mount_bucket_folder = "";
string mountpoint = "";
string config_file = "";
bool foreground = false;
CosAPI* cos_client = NULL;
const string CONFIG_FILE_NAME = "cosfs_config.txt";
pthread_t ntid = 0;//monitor pid


int my_fuse_opt_proc(void* data, const char* arg, int key, struct fuse_args* outargs)
{
    //std::cout << "key=" << key << ",arg=" << arg << std::endl;
    if(key == FUSE_OPT_KEY_NONOPT){
        // the first NONOPT option is the bucket name
        if(bucket.size() == 0){
            string bucket_name(arg);
            size_t index = 0;
            if((index = bucket_name.find_first_of(':')) != string::npos )
            {
                bucket = bucket_name.substr(0, index);
                mount_bucket_folder = bucket_name.substr(index + 1);
                int len = mount_bucket_folder.length();
                //如果文件夹路径以'/'结尾，则删除'/',便于后续的组装路径
                if (mount_bucket_folder.at(len -1) == '/')
                {
                    mount_bucket_folder = mount_bucket_folder.substr(0,len - 1);
                }
            } 
            else 
            {
                bucket = bucket_name;
                mount_bucket_folder = "";
            }

            return 0;
        }

        // the second NONPOT option is the mountpoint(not utility mode)
        if(0 == mountpoint.size()){
            // save the mountpoint and do some basic error checking
            mountpoint = arg;
            struct stat stbuf;

            if(stat(arg, &stbuf) == -1){
                S3FS_PRN_EXIT("unable to access MOUNTPOINT %s: %s", mountpoint.c_str(), strerror(errno));
                return -1;
            }
            if(!(S_ISDIR(stbuf.st_mode))){
                S3FS_PRN_EXIT("MOUNTPOINT: %s is not a directory.", mountpoint.c_str());
                return -1;
            }

            if(!nonempty){
                struct dirent *ent;
                DIR *dp = opendir(mountpoint.c_str());
                if(dp == NULL){
                    S3FS_PRN_EXIT("failed to open MOUNTPOINT: %s: %s", mountpoint.c_str(), strerror(errno));
                    return -1;
                }
                while((ent = readdir(dp)) != NULL){
                    if(strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0){
                        closedir(dp);
                        S3FS_PRN_EXIT("MOUNTPOINT directory %s is not empty. if you are sure this is safe, can use the 'nonempty' mount option.", mountpoint.c_str());
                        return -1;
                    }
                }
                closedir(dp);
            }
            return 1;
        }
    } 
    else if (key == FUSE_OPT_KEY_OPT) {

        if(0 == STR2NCMP(arg, "cfgfile=")) {
            const char *p = strchr(arg, '=') + sizeof(char);
            if (p != NULL){
                config_file.assign(p);
            }
            else {
                S3FS_PRN_EXIT("config file can't null");
                return -1;
            }

            return 0;
        }

        if(0 == STR2NCMP(arg, "dbglevel=")) {
            const char* strlevel = strchr(arg, '=') + sizeof(char);
            if(0 == strcasecmp(strlevel, "critical") || 0 == strcasecmp(strlevel, "crit")){
                set_s3fs_log_level(S3FS_LOG_CRIT);
            }else if(0 == strcasecmp(strlevel, "error") || 0 == strcasecmp(strlevel, "err")){
                set_s3fs_log_level(S3FS_LOG_ERR);
            }else if(0 == strcasecmp(strlevel, "wan") || 0 == strcasecmp(strlevel, "warn") || 0 == strcasecmp(strlevel, "warning")){
                set_s3fs_log_level(S3FS_LOG_WARN);
            }else if(0 == strcasecmp(strlevel, "inf") || 0 == strcasecmp(strlevel, "info") || 0 == strcasecmp(strlevel, "information")){
                set_s3fs_log_level(S3FS_LOG_INFO);
            }else if(0 == strcasecmp(strlevel, "dbg") || 0 == strcasecmp(strlevel, "debug")){
                set_s3fs_log_level(S3FS_LOG_DBG);
            }else{
                S3FS_PRN_EXIT("option dbglevel has unknown parameter(%s).", strlevel);
                return -1;
            }
            return 0;
        }
            
    }

    return 1;
}

void sigterm_handler(int )
{
    S3FS_PRN_EXIT("sigterm handler, exit");
    exit(0);
}

int main(int argc, char* argv[])
{
    int ch;
    int fuse_res;
    struct fuse_operations cosfs_oper;
    char *config_name = NULL;
    int option_index = 0;
    signal(SIGTERM, sigterm_handler);

    openlog("cosfs", LOG_PID | LOG_ODELAY | LOG_NOWAIT, LOG_USER);
    set_s3fs_log_level(debug_level);

    size_t found = string::npos;
    program_name.assign(argv[0]);
    found = program_name.find_last_of("/");
    if(found != string::npos){
        program_name.replace(0, found+1, "");
    }

    static const struct option long_opts[] = {
        {"help",      no_argument, NULL, 'h'},
        {"version",   no_argument, NULL, 'v'},
        {"config",    required_argument, NULL, 'c'},
        {0, 0, 0, 0}
    };
    const char *short_opts = "hfvo:c:";
    while((ch = getopt_long(argc, argv, short_opts, long_opts, &option_index)) != -1){
        switch(ch){
            case 'v':
                CommonUtil::show_version();
                exit(EXIT_SUCCESS);
            case 'h':
                CommonUtil::show_help();
                exit(EXIT_SUCCESS);
            case 'c':
                config_name  = optarg;
                break;
            case 'f':
                foreground  = true;
                break;
            case 'o':
                break;
            default:
                exit(EXIT_FAILURE);
        }
    }

    struct fuse_args custom_args = FUSE_ARGS_INIT(argc, argv);
    if(0 != fuse_opt_parse(&custom_args, NULL, NULL, my_fuse_opt_proc)){
        exit(EXIT_FAILURE);
    }

    CosInfo::bucket = bucket;
    CosInfo::mount_bucket_folder = mount_bucket_folder;
    CosInfo::mountpoint = mountpoint;

    if (bucket.size() == 0){
       S3FS_PRN_EXIT("bucket invalid, exit");
       exit(EXIT_FAILURE); 
    }

    if (mountpoint.size() == 0){
       S3FS_PRN_EXIT("mountpoint invalid, exit");
       exit(EXIT_FAILURE); 
    }

    //从配置文件读取配置信息(需要在解析完命令行参数之后解析,内部用到CosInfo::bucket)
    if (!CommonUtil::parseConfig(config_file.c_str())){
        S3FS_PRN_EXIT("parseConfig(%s) fail, exit",config_file.c_str());
        exit(EXIT_FAILURE);
    }

    CosConfig config(config_file);
    cos_client = new CosAPI(config);
    if (!cos_client)
    {
        S3FS_PRN_EXIT("new cos_client fail, exit");
        exit(EXIT_FAILURE);
    }

    if (!CommonUtil::checkCosInfoValid())
    {
        if (CosInfo::mount_bucket_folder.empty())
        {
            S3FS_PRN_EXIT("COS info(appid[%lu],bucket[%s],secret_id[%s],secret_key[%s]) not valid, exit",
            CosInfo::appid,CosInfo::bucket.c_str(),CosInfo::secret_id.c_str(),CosInfo::secret_key.c_str());
        }
        else
        {
            S3FS_PRN_EXIT("COS info(appid[%lu],bucket[%s],mount_folder[%s],secret_id[%s],secret_key[%s]) not valid, exit",
            CosInfo::appid,CosInfo::bucket.c_str(),CosInfo::mount_bucket_folder.c_str(),CosInfo::secret_id.c_str(),CosInfo::secret_key.c_str());
        }
        
        exit(EXIT_FAILURE);
    }

    memset(&cosfs_oper, 0, sizeof(cosfs_oper));
    cosfs_oper.init      = &CosFS::cosfs_init;
    cosfs_oper.destroy   = &CosFS::cosfs_destroy;
    cosfs_oper.open      = &CosFile::cosfs_open;
    cosfs_oper.read      = &CosFile::cosfs_read;
    cosfs_oper.release   = &CosFile::cosfs_release;
    cosfs_oper.statfs    = &CosFile::cosfs_statfs;
    cosfs_oper.getattr   = &CosFile::cosfs_getattr;
    cosfs_oper.opendir   = &CosDir::cosfs_opendir;
    cosfs_oper.readdir   = &CosDir::cosfs_readdir;
    cosfs_oper.access    = &CosFile::cosfs_access;
    cosfs_oper.write     = &CosFile::cosfs_write;
    cosfs_oper.link      = &CosFS::cosfs_link;
    cosfs_oper.rmdir     = &CosFS::cosfs_rmdir;
    cosfs_oper.rename    = &CosFS::cosfs_rename;
    cosfs_oper.symlink   = &CosFS::cosfs_symlink;  

    fuse_res = fuse_main(custom_args.argc, custom_args.argv, &cosfs_oper, NULL);

    //退出程序
    fuse_opt_free_args(&custom_args);
    delete cos_client;
    S3FS_PRN_EXIT("main exit .......");
    exit(fuse_res);
}
