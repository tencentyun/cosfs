#include "common.h"
#include "common_util.h"
#include <stdint.h>
#include <string.h>
#include <string>
#include <signal.h>
#include <stdlib.h>
#include <iostream>
#include <stdio.h>
#include <memory.h>
#include "json/json.h"
#include "cos_log.h"
#include <fstream>
#include "cos_info.h"
#include "cosfs_util.h"
#include "cos_defines.h"

using namespace std;
using qcloud_cos::Auth;

void CommonUtil::s3fs_usr2_handler(int sig)
{
    /*
    if(SIGUSR2 == sig){
        bumpup_s3fs_log_level();
    }
    */
}

bool CommonUtil::set_s3fs_usr2_handler(void)
{
/*
    struct sigaction sa;

    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_handler = s3fs_usr2_handler;
    sa.sa_flags   = SA_RESTART;
    if(0 != sigaction(SIGUSR2, &sa, NULL)){
        return false;
    }
*/
    return true;
}

void CommonUtil::show_version(void)
{
    printf("cosfs v3.3.0\n");
    return;
}

void CommonUtil::show_help(void)
{
    printf("Usage: cosfs BUCKET MOUNTPOINT [OPTION]...\n");
    printf(
    "\n"
    "Mount an QCloud Cos bucket as a file system.\n"
    "\n"
    "   General forms for cosfs and FUSE/mount options:\n"
    "      -o opt [-o opt] ...\n"
    "\n"
    "cosfs Options:\n"
    "\n"
    "   Most cosfs options are given in the form where \"opt\" is:\n"
    "\n"
    "   <option_name>=<option_value>\n"
    "\n"
    "   cfgfile \n"
    "       Set the configfile, it configs the information of COS and other configuration\n"
    "\n"
    "   dbglevel (default=\"err\")\n" 
    "       Set the debug message level. set value as crit(critical), err\n"
    "       (error), warn(warning), info(information) to debug level.\n"
    "       default debug level is critical.\n"
    "\n"
    "Miscellaneous Options:\n"
    "\n"
    " -h, --help        Output this help.\n"
    " -v, --version     Output version info.\n"
    "\n"
    );            
    return;
}

string CommonUtil::getCosDomain()
{
    return CosInfo::kApiCosapiEndpoint;
}

bool CommonUtil::checkCosInfoValid()
{
    string cospath(CosInfo::mount_bucket_folder + "/");
    FolderStatReq folderStatReq(bucket,cospath);
    string result = cos_client->FolderStat(folderStatReq);
    Json::Value ret = StringUtil::StringToJson(result);
    if (!ret.isMember("code"))
    {
        S3FS_PRN_ERR("checkCosInfoValid: statFolder(%s) error, return:%s ",cospath.c_str(), result.c_str());
        return false;
    }

    if (0 != ret["code"].asInt())
    {
        S3FS_PRN_ERR("checkCosInfoValid: statFolder(%s) error, return:%s ",cospath.c_str(), result.c_str());
        return false;
    }

    return true;
}

string CommonUtil::getCosHostHeader()
{
    return CosInfo::kApiCosapiEndpoint.substr(strlen("http://"));
}

string CommonUtil::GetDownLoadSign()
{
    uint64_t expire = time(NULL) + SIGN_EXPIRE_TIME;
    return Auth::AppSignMuti(CosInfo::appid,CosInfo::secret_id, CosInfo::secret_key,expire,CosInfo::bucket);
}

bool CommonUtil::parseConfig(const char* pconfig_file)
{
    string file_config("cosfs_config.txt");
    if(NULL != pconfig_file){
        file_config.assign(pconfig_file);
    }

    std::ifstream is(file_config.c_str(), std::ios::in);
    if (!is)
    {
        S3FS_PRN_ERR("parseConfig(%s) error",file_config.c_str());
        return false;
    }

    Json::Value root; 
    Json::Reader reader;
    string domain;
    if (reader.parse(is, root, false))
    {
        S3FS_PRN_DBG("parseConfig: config: %s ", root.toStyledString().c_str());
        if (root.isMember(CONFIG_APPID)){
            CosInfo::appid = root[CONFIG_APPID].asUInt64();
        } else {
            S3FS_PRN_ERR("parseConfig(%s) error, appid unavailable",file_config.c_str());
            is.close();
            return false;
        }

        if (root.isMember(CONFIG_SECRET_ID)){
            CosInfo::secret_id = root[CONFIG_SECRET_ID].asString();
        } else {
            S3FS_PRN_ERR("parseConfig(%s) error, secret_id unavailable",file_config.c_str());
            is.close();
            return false;
        }

        if (root.isMember(CONFIG_SECRET_KEY)) {
            CosInfo::secret_key = root[CONFIG_SECRET_KEY].asString();
        } else {
            S3FS_PRN_ERR("parseConfig(%s) error, secret_key unavailable",file_config.c_str());
            is.close();
            return false;
        }
    } else {
        S3FS_PRN_ERR("parseConfig(%s) reader.parse error",file_config.c_str());
        is.close();

        return false;
    }

    is.close();
    return true;
}

