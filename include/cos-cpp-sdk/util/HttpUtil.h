#ifndef HTTP_UTIL_H
#define HTTP_UTIL_H
#include <string>
#include <map>
#include <stdint.h>

using std::string;
using std::map;

namespace qcloud_cos{

class HttpUtil
{
    public:
        HttpUtil();
        ~HttpUtil();  
        static string GetEncodedCosUrl(const string& endpoint,
                                      const string& bucketName,
                                      const string& dstPath,
                                      uint64_t appid);
        static string GetEncodedCosUrl(uint64_t appid,const std::string& bucket_name,const std::string& path);
        static string GetEncodedDownloadCosCdnUrl(
                    uint64_t  appid,
                    const string& bucketName,
                    const string& dstPath,
                    const string& domain,
                    const string& sign);
        static string GetEncodedDownloadCosUrl(
                    uint64_t  appid,
                    const string& bucketName,
                    const string& dstPath,
                    const string& domain,
                    const string& sign) ;
        static string GetEncodedDownloadCosUrl(
                    const string& domain,
                    const string& dstPath,
                    const string& sign) ;
};

}

#endif
