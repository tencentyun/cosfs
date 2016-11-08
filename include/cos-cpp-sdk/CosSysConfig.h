#ifndef COS_SYS_CONF_H
#define COS_SYS_CONF_H
#include "CosDefines.h"
#include <stdint.h>
#include <pthread.h>
#include "curl/curl.h"

namespace qcloud_cos{


class CosSysConfig {
   
public:
    //设置签名超时时间,单位:秒
    static void setExpiredTime(uint64_t time);
    //设置连接超时时间,单位:豪秒
    static void setTimeoutInms(uint64_t time);
    //设置全局超时时间,单位:豪秒
    static void setGlobalTimeoutInms(uint64_t time);
    //设置上传分片大小,默认:1M
    static void setSliceSize(uint64_t sliceSize);
    //设置L5模块MODID
    static void setL5Modid(int64_t l5_modid);
    //设置L5模块CMDID
    static void setL5Cmdid(int64_t l5_cmdid);
    //设置上传文件是否携带sha值,默认:不携带
    static void setIsTakeSha(bool takeSha);
    //设置cos区域和下载域名:cos,cdn,innercos,自定义,默认:cos
    static void setRegionAndDLDomain(string region, DLDomainType type, string domain = "");
    //设置文件分片并发上传线程池大小,默认: 5
    static void setUploadThreadPoolSize(unsigned size);
    //设置异步上传下载线程池大小,默认: 2
    static void setAsynThreadPoolSize(unsigned size);
    //设置log输出,1:屏幕,2:syslog,3:不输出,默认:1
    static void setLogOutType(LOG_OUT_TYPE log);

    static uint64_t getExpiredTime();
    static uint64_t getTimeoutInms();
    static uint64_t getGlobalTimeoutInms();
    static uint64_t getSliceSize();
    static int64_t getL5Modid();
    static int64_t getL5Cmdid();
    static bool isTakeSha();
    static string getDownloadDomain();
    static string getUploadDomain();
    static DLDomainType getDownloadDomainType();
    static unsigned getUploadThreadPoolSize();
    static unsigned getAsynThreadPoolSize();
    static int getLogOutType();
    static void print_value();
private:
    //打印日志:0,不打印,1:打印到屏幕,2:打印到syslog
    static LOG_OUT_TYPE log_outtype;
    //上传文件是否携带sha
    static bool takeSha;
    //上传分片大小
    static uint64_t sliceSize;
    //签名超时时间(秒)
    static uint64_t expire_in_s;
    // 超时时间(毫秒)
    static uint64_t timeout_in_ms;
    // 全局超时时间(毫秒)
    static uint64_t timeout_for_entire_request_in_ms;
    static int64_t  l5_modid;
    static int64_t  l5_cmdid;
    //下载域名类型
    static DLDomainType domainType;
    //下载域名
    static string   downloadDomain;
    //cos所属区域
    static string   m_region;
    //文件上传域名
    static string   m_uploadDomain;
    //单文件分片并发上传线程池大小(每个文件一个)
    static unsigned m_threadpool_size;
    //异步上传下载线程池大小(全局就一个)
    static unsigned m_asyn_threadpool_size;
};

}

#endif
