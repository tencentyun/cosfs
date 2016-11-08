#ifndef FILE_UPLOAD_TASK_H
#define FILE_UPLOAD_TASK_H
#pragma once

#include <pthread.h>
#include <string>
#include "op/BaseOp.h"
#include "util/FileUtil.h"
#include "util/HttpUtil.h"
#include "util/CodecUtil.h"
#include "util/HttpSender.h"
#include "util/StringUtil.h"
#include "request/CosResult.h"
#include "CosParams.h"
#include "CosDefines.h"
#include "CosConfig.h"
#include "CosSysConfig.h"

using std::string;
namespace qcloud_cos{

class FileUploadTask {

public:
    FileUploadTask(const string& url, const string& session, 
        const string& sign, const string& sha);
    FileUploadTask(const string& url, const string& session, 
        const string& sign, const string& sha, uint64_t offset, 
        unsigned char* pbuf, const size_t datalen);
    ~FileUploadTask(){};
    void run();
    void uploadTask();
    void setOffset(uint64_t offset);
    void setBufdata(unsigned char* pdatabuf, size_t datalen);
    bool taskSuccess();
    string getTaskResp();
private:
    FileUploadTask();

private:
    string m_url;
    string m_session;
    string m_sign;
    string m_sha;
    uint64_t m_offset;
    unsigned char*  m_pdatabuf;
    size_t m_datalen;
    string m_resp;
    bool m_task_success;
};

}


#endif
