// Copyright (c) 2016, Tencent Inc.
// All rights reserved.
//
// Author: Wu Cheng <chengwu@tencent.com>
// Created: 03/10/2016
// Description:

#ifndef HTTPSENDER_H
#define HTTPSENDER_H
#pragma once

#include <string>
#include <map>
#include <curl/curl.h>
#include <curl/multi.h>

#define MAX_WAIT_MSECS 60       /* Max wait timeout, 30 seconds */

using std::string;
namespace qcloud_cos {
class HttpSender {
private:

    /**
     * @brief 设置HTTP返回值的回调函数
     *
     * @param buffer  返回值暂存的buffer
     * @param size    buffer中每一个对象的大小
     * @param count   对象的个数
     * @param stream  保存返回值的地址
     *
     * @return        返回返回值的字节长度
     */
    static size_t CurlWriter(void* buffer, size_t size, size_t count, void* stream);


    /**
     * @brief 生成一个easy curl对象，并设置一些公共值
     *
     * @param url      url地址
     * @param rsp      保存返回值的std::string对象
     * @param is_post  是否是post请求
     * @param option   用户的一些配置选项，包括超时时间(毫秒)等
     *
     * @return    返回easy_curl对象
     */
    static CURL *CurlEasyHandler(const std::string& url, std::string* rsp,
                                 bool is_post);

    /**
     * @brief 设置easy curl的HTTP头部
     *
     * @param curl_handler    easy_curl类型的头部
     * @param user_headers    用户自定义的http头部map
     *
     * @return 返回curl对象的头部链
     */
    static struct curl_slist* SetCurlHeaders(CURL* curl_handler, const std::map<std::string, std::string>& user_headers);

    /**
     * @brief 返回一个multipart/form-data类型的CURL对象
     *
     * @param url                   url地址
     * @param user_headers          http头部
     * @param user_params           http参数
     * @param fileContent           要发送的文件内容
     * @param fileContent_len       发送的文件长度
     * @param option   用户的一些配置选项，包括超时时间(毫秒)等
     * @param firstitem             返回的要上传的第一个form-data元素的对象,用于后续的释放
     * @param header_lists          返回设置的头部header_list
     * @param reponse               存储返回值的字符串
     *
     * @return              返回CURL对象
     */
    static CURL *PrepareMultiFormDataCurl(const std::string& url,
                                          const std::map<std::string, std::string>& user_headers,
                                          const std::map<std::string, std::string>& user_params,
                                          const unsigned char* fileContent,
                                          const unsigned int fileContent_len,
                                          struct curl_httppost* &firstitem,
                                          struct curl_slist* &header_lists,
                                          std::string* reponse);
public:

    /**
     * @brief                   发送get请求
     *
     * @param url               url地址
     * @param user_headers      自定义的HTTP头
     * @param user_params       自定义的参数
     *
     * @return                  server返回应答
     */
    static std::string SendGetRequest(const std::string url,
                                      const std::map<std::string, std::string>& user_headers,
                                      const std::map<std::string, std::string>& user_params);

    /**
     * @brief                   发送get请求
     *
     * @param url               url地址
     * @param user_headers      自定义的HTTP头
     * @param user_params       自定义的参数
     *
     * @return                  server返回http code，将应答保存到pRsp中
     */
    static int SendGetRequest(string* pRsp, const string& url,
                const std::map<string, string> &user_headers,
                const std::map<string, string> &user_params);

    /**
     * @brief                   发送json post请求
     *
     * @param url               url地址
     * @param user_headers      自定义的HTTP头
     * @param user_params       自定义的参数
     * @param option   用户的一些配置选项，包括超时时间(毫秒)等
     *
     * @return                  server返回应答
     */
    static std::string SendJsonPostRequest(const std::string url,
                                           const std::map<std::string, std::string>& user_headers,
                                           const std::map<std::string, std::string>& user_params);

    /**
     * @brief                   发送json post请求
     *
     * @param url               url地址
     * @param user_headers      自定义的HTTP头
     * @param jsonBody       body
     * @param option   用户的一些配置选项，包括超时时间(毫秒)等
     *
     * @return                  server返回应答
     */
    static std::string SendJsonBodyPostRequest(const string url,const std::string& jsonBody,
                                           const std::map<string, string> &user_headers);

    /**
     * @brief                       发送文件上传post请求, 类型为multipart/form-data
     *
     * @param url                   url地址
     * @param user_headers          用户自定的http头
     * @param user_params           用户自定义的参数
     * @param fileContent           文件内容
     * @param fileContent_len       文件长度
     * @param option   用户的一些配置选项，包括超时时间(毫秒)等
     *
     * @return                      server返回的应答
     */
    static std::string SendSingleFilePostRequest(const std::string& url,
                                                 const std::map<std::string, std::string>& user_headers,
                                                 const std::map<std::string, std::string>& user_params,
                                                 const unsigned char* fileContent,
                                                 const unsigned int fileContent_len);

    /**
     * @brief                       并行发送文件
     *
     * @param url                   文件的url地址
     * @param user_headers          用户的http头
     * @param user_params           用户的自定义参数
     * @param localFileName         本地文件地址
     * @param offset                偏移量
     * @param sliceSize             分片大小
     * @param option   用户的一些配置选项，包括超时时间(毫秒)等
     *
     * @return                      server返回的应答
     */
    static std::string SendFileParall(const std::string url,
                                      const std::map<std::string, std::string> user_headers,
                                      const std::map<std::string, std::string> user_params,
                                      const std::string localFileName,
                                      unsigned long offset,
                                      const unsigned long sliceSize);

    static int64_t GetTimeStampInUs();
};

} // namespace qcloud_cos
#endif // HTTPSENDER_H
