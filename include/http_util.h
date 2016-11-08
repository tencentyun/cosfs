#ifndef HTTP_UTILS_H
#define HTTP_UTILS_H
#pragma once

#include <string>
#include <map>
#include <stdint.h>
#include <curl/curl.h>
#include <curl/multi.h>
#include "cos_log.h"

using std::string;
class HttpUtils {

private:
    static size_t CurlWriter(void* buffer, size_t size, size_t count, void* stream);
    static CURL *CurlEasyHandler(const std::string& url, std::string* rsp,
                                 bool is_post);
    static struct curl_slist* SetCurlHeaders(CURL* curl_handler, 
                  const std::map<std::string, std::string>& user_headers);
    static int64_t GetTimeStampInUs();
    static string UrlEncode(const string &str);
    static string EncodePath(const string &dstPath);
    static unsigned char ToHex(const unsigned char &x);
public:
    static string GetEncodedCosUrl(const string& dstPath);
    static int SendRequestReadCosFile(string* pRsp,const string& url, const uint64_t offset, const uint64_t length);
    static int SendRequestReadCosFile(string* pRsp,const string& url, const uint64_t offset);
    static int SendRequestReadCosFile(string* pRsp,const string& url);
    static int SendGetRequest(string* pRsp,const string& url,const std::map<string, string> &user_headers);
    static int SendGetRequest(string* pRsp,const std::string& url,
                                      const std::map<std::string, std::string>& user_headers,
                                      const std::map<std::string, std::string>& user_params);
};

#endif // HTTPSENDER_H
