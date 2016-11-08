#ifndef COS_DEFINE_H
#define COS_DEFINE_H
#include <string>
#include <stdio.h>
#include <syslog.h>
using std::string;
namespace qcloud_cos{

const string kPathDelimiter = "/";
const unsigned char kPathDelimiterChar = '/';
// 分片上传时，失败的最大重试次数
const int kMaxRetryTimes = 3;

const int DEFAULT_POOL_SIZE = 2;
const int DEFAULT_THREAD_POOL_SIZE_UPLOAD_SLICE = 5;
const int MAX_THREAD_POOL_SIZE_UPLOAD_SLICE = 10;
const int MIN_THREAD_POOL_SIZE_UPLOAD_SLICE = 1;

const int SINGLE_FILE_SIZE = 8 * 1024 * 1024;
const int SLICE_SIZE_512K = 512 * 1024;
const int SLICE_SIZE_1M = 1 * 1024 * 1024;
const int SLICE_SIZE_2M = 2 * 1024 * 1024;
const int SLICE_SIZE_3M = 3 * 1024 * 1024;

extern int out_log;
typedef enum DownloadDomainType{
    DOMAIN_INVALID      = 0,
    DOMAIN_CDN,
    DOMAIN_COS,
    DOMAIN_INNER_COS,
    DOMAIN_SELF_DOMAIN
}DLDomainType;

typedef enum log_out_type{
    COS_LOG_NULL = 0,
    COS_LOG_STDOUT,
    COS_LOG_SYSLOG
}LOG_OUT_TYPE;

typedef enum cos_log_level{
    COS_LOG_ERR  = 1,          // LOG_ERR
    COS_LOG_WARN = 3,          // LOG_WARNING
    COS_LOG_INFO = 7,          // LOG_INFO
    COS_LOG_DBG  = 15          // LOG_DEBUG
}LOG_LEVEL;

#define LOG_LEVEL_STRING(level) \
        ( COS_LOG_DBG  == (level & COS_LOG_DBG) ? "[DBG] " :    \
          COS_LOG_INFO == (level & COS_LOG_INFO) ? "[INFO] " :  \
          COS_LOG_WARN == (level & COS_LOG_WARN) ? "[WARN] " :  \
          COS_LOG_ERR  == (level & COS_LOG_ERR) ? "[ERR] " : "[CRIT]")

#define COS_LOW_LOGPRN(level, fmt, ...) \
    if (CosSysConfig::getLogOutType()== COS_LOG_STDOUT) { \
       fprintf(stdout,"%s:%s(%d) " fmt "%s\n", LOG_LEVEL_STRING(level),__func__,__LINE__, __VA_ARGS__); \
    }else if (CosSysConfig::getLogOutType() == COS_LOG_SYSLOG){ \
       syslog(LOG_INFO,"%s:%s(%d) " fmt "%s\n", LOG_LEVEL_STRING(level),__func__,__LINE__, __VA_ARGS__); \
    } else { \
    }        \

#define SDK_LOG_DBG(fmt, ...)           COS_LOW_LOGPRN(COS_LOG_DBG,  fmt, ##__VA_ARGS__, "")
#define SDK_LOG_INFO(fmt, ...)          COS_LOW_LOGPRN(COS_LOG_INFO,  fmt, ##__VA_ARGS__, "")
#define SDK_LOG_WARN(fmt, ...)          COS_LOW_LOGPRN(COS_LOG_WARN,  fmt, ##__VA_ARGS__, "")
#define SDK_LOG_ERR(fmt, ...)           COS_LOW_LOGPRN(COS_LOG_ERR,  fmt, ##__VA_ARGS__, "")
#define SDK_LOG_COS(level, fmt, ...)    COS_LOW_LOGPRN(level,  fmt, ##__VA_ARGS__, "")

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

}
#endif
