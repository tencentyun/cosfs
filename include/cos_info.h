#ifndef COS_INFO_H
#define COS_INFO_H
#include <string>
#include <stdint.h>
using std::string;

class CosInfo {
public:
    static uint64_t appid;
    static string secret_id;
    static string secret_key;
    static string bucket;
    static string mount_bucket_folder;    
    static string mountpoint;
    static string domain;
    static string cos_sdk_config_file;
    static string kApiCosapiEndpoint;
};

#endif
