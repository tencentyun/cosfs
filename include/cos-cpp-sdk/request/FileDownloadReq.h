#ifndef FILE_DOWNLOAD_REQ_H
#define FILE_DOWNLOAD_REQ_H
#include "ReqBase.h"
#include <stdint.h>
#include <string>
#include <iostream>
#include <fstream>

using std::string;
namespace qcloud_cos{

class FileDownloadReq : public ReqBase
{
    public:
        friend class FileOp;
    public:
        FileDownloadReq(string& bucket,string& filePath);
        string getFilePath();
        string toJsonString();
        string getDownloadUrl();
        virtual bool isParaValid(CosResult& cosResult);
        FileDownloadReq(const FileDownloadReq& req);
        FileDownloadReq(){};
    private:
        string   _filePath;
};

}

#endif
