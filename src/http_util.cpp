// Copyright (c) 2016, Tencent Inc.
// All rights reserved.
//
// Author: Wu Cheng <chengwu@tencent.com>
// Created: 03/08/2016
// Description:
#include "http_util.h"
#include <string>
#include <iostream>
#include <fstream>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <cstring>
#include <stdint.h>
#include "json/json.h"
#include "string_util.h"
#include "cosfs_util.h"
#include "cos_log.h"
#include "common_util.h"

using std::string;
using std::size_t;

const string _kPathDelimiter = "/";
const unsigned char _kPathDelimiterChar = '/';
const uint64_t _conn_timeout = 30*1000;
const uint64_t _global_conn_timeout = 300*1000;

string HttpUtils::GetEncodedCosUrl(const string& dstPath) 
{
    string domain = CommonUtil::getCosDomain();
    string sign = CommonUtil::GetDownLoadSign();
    string url = domain.substr(strlen("http://")).append(dstPath);
    return "http:/"+EncodePath(url).append("?sign=").append(sign);
}

string HttpUtils::EncodePath(const string &dstPath) {
    string encodeStr = "";
    if (dstPath.empty()) {
        return encodeStr;
    }
    if (dstPath.find(_kPathDelimiter) != 0) {
        encodeStr.append(_kPathDelimiter);
    }
    size_t total_len = dstPath.length();
    size_t pos = 0;
    size_t next_pos = 0;
    string tmp_str;
    while (pos < total_len) {
        next_pos = dstPath.find(_kPathDelimiter, pos);
        if (next_pos == string::npos) {
            next_pos = total_len;
        }
        tmp_str = dstPath.substr(pos, next_pos - pos);
        StringUtil::Trim(tmp_str);
        if (!tmp_str.empty()) {
            encodeStr.append(UrlEncode(tmp_str));
            encodeStr.append(_kPathDelimiter);
        }
        pos = next_pos + 1;
    }

    // 如果原路径是文件，则删除最后一个分隔符
    if (dstPath[total_len - 1] != _kPathDelimiterChar) {
        encodeStr.erase(encodeStr.length() - 1, 1);
    }
    return encodeStr;
}

string HttpUtils::UrlEncode(const string &str) {
    string encodedUrl = "";
    std::size_t length = str.length();
    for (size_t i = 0; i < length; ++i) {
        if (isalnum((unsigned char)str[i]) ||
            (str[i] == '-') ||
            (str[i] == '_') ||
            (str[i] == '.') ||
            (str[i] == '~') ||
            (str[i] == '/')) {
            encodedUrl += str[i];
        } else {
            encodedUrl += '%';
            encodedUrl += ToHex((unsigned char)str[i] >> 4);
            encodedUrl += ToHex((unsigned char)str[i] % 16);
        }
    }
    return encodedUrl;
}

unsigned char HttpUtils::ToHex(const unsigned char &x) {
    return x > 9 ? (x - 10 + 'A') : x + '0';
}

size_t HttpUtils::CurlWriter(void *buffer, size_t size, size_t count, void *stream) {
    string *pstream = static_cast<string *>(stream);
    (*pstream).append((char *)buffer, size * count);
    return size * count;
}

/*
 * 生成一个easy curl对象，并设置一些公共值
 */
CURL *HttpUtils::CurlEasyHandler(const string &url, string *rsp, bool is_post) {
    CURL *easy_curl = curl_easy_init();
    uint64_t conn_timeout = _conn_timeout;
    uint64_t global_timeout = _global_conn_timeout;
    curl_easy_setopt(easy_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy_curl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(easy_curl, CURLOPT_TIMEOUT_MS, global_timeout);
    curl_easy_setopt(easy_curl, CURLOPT_CONNECTTIMEOUT_MS, conn_timeout);
    curl_easy_setopt(easy_curl, CURLOPT_SSL_VERIFYHOST, 0);
    curl_easy_setopt(easy_curl, CURLOPT_SSL_VERIFYPEER, 1);

    if (is_post) {
        curl_easy_setopt(easy_curl, CURLOPT_POST, 1);
    }

    curl_easy_setopt(easy_curl, CURLOPT_WRITEFUNCTION, CurlWriter);
    curl_easy_setopt(easy_curl, CURLOPT_WRITEDATA, rsp);
    return easy_curl;
}

struct curl_slist* HttpUtils::SetCurlHeaders(CURL *curl_handler, const std::map<string, string> &user_headers) {
    struct curl_slist *header_lists = NULL;
    header_lists = curl_slist_append(header_lists, "Accept: */*");
    header_lists = curl_slist_append(header_lists, "Connection: keep-alive");
    header_lists = curl_slist_append(header_lists, "User-Agent: cosfs-v3.3.1");

    std::map<string, string>::const_iterator it = user_headers.begin();
    string header_key, header_value, full_str;
    for (; it != user_headers.end(); ++it) {
        header_key = it->first;
        header_value = it->second;
        full_str = header_key + ": " + header_value;
        header_lists = curl_slist_append(header_lists, full_str.c_str());
    }
    curl_easy_setopt(curl_handler, CURLOPT_HTTPHEADER, header_lists);
    return header_lists;
}

int HttpUtils::SendRequestReadCosFile(string* pRsp,const string& url, const uint64_t offset, const uint64_t size)
{
    string value = "bytes=" + str(offset) + "-" + str(offset + size -1);
    std::map<string, string> user_headers;
    user_headers["Range"] = value;
    string host = CommonUtil::getCosHostHeader();
    user_headers["Host"] = host;
    return SendGetRequest(pRsp,url,user_headers);
}

int HttpUtils::SendRequestReadCosFile(string* pRsp, const string& url, const uint64_t offset)
{
    string value = "bytes=" + str(offset) + "-";
    std::map<string, string> user_headers;
    user_headers["Range"] = value;
    user_headers["Host"] = CommonUtil::getCosHostHeader();
    return SendGetRequest(pRsp,url,user_headers);
}

int HttpUtils::SendRequestReadCosFile(string* pRsp, const string& url)
{
    std::map<string, string> user_headers;
    user_headers["Host"] = CommonUtil::getCosHostHeader();
    std::map<string, string> user_params;
    return SendGetRequest(pRsp,url,user_headers,user_params);
}

int HttpUtils::SendGetRequest(string* pRsp, const string& url,const std::map<string, string> &user_headers)
{
    std::map<string, string> user_params;
    return SendGetRequest(pRsp,url,user_headers,user_params);
}
int HttpUtils::SendGetRequest(string* pRsp, const string& url,
                const std::map<string, string> &user_headers,
                const std::map<string, string> &user_params) {
    string user_params_str = "";
    string param_key = "";
    string param_value = "";
    std::map<string, string>::const_iterator it = user_params.begin();
    for (; it != user_params.end(); ++it) {
        if (!user_params_str.empty()) {
            user_params_str += '&';
        }
        param_key = it->first;
        param_value = it->second;
        user_params_str += param_key + "=" + param_value;
    }
    string full_url(url);
    if (!user_params_str.empty())
    {
        if (full_url.find('?') ==string::npos) {
            full_url += "?" + user_params_str;
        } else {
            full_url += "&" + user_params_str;
        }
    }

    S3FS_PRN_INFO("SendGetRequest: full_url = %s",full_url.c_str());

    CURL* get_curl = CurlEasyHandler(full_url, pRsp, false);
    curl_slist* header_lists = SetCurlHeaders(get_curl, user_headers);
    CURLcode ret_code = curl_easy_perform(get_curl);

    long http_code = -1;
    if (ret_code == CURLE_OK) {
        ret_code = curl_easy_getinfo(get_curl, CURLINFO_RESPONSE_CODE , &http_code);
    } else {
        S3FS_PRN_ERR("SendGetRequest: error,ret_code=%d, full_url = %s",ret_code, full_url.c_str());
    }

    curl_slist_free_all(header_lists);
    curl_easy_cleanup(get_curl);

    return http_code;
}

int64_t HttpUtils::GetTimeStampInUs() {
    // 构造时间
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}
