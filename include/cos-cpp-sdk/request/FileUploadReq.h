#ifndef FILE_UPLOAD_REQ_H
#define FILE_UPLOAD_REQ_H
#include "ReqBase.h"
#include <stdint.h>
#include <string>
#include <iostream>
#include <fstream>

using std::string;
namespace qcloud_cos{

class FileUploadReq : public ReqBase
{
    public:
        FileUploadReq(string& bucket,string& srcPath, string& cosPath, int insertOnly=1);
        FileUploadReq(string& bucket,string& cosPath, const char* pbuffer, uint64_t buflen, int insertOnly=1);
        FileUploadReq(const FileUploadReq &req);
        void setBizAttr(string& bizAttr);
        void setInsertOnly(int insertOnly);
        void setSliceSize(int sliceSize);
        string getBucket();
        string getBizAttr();
        string getSrcPath();
        string getDstPath();
        int getSliceSize();
        int getInsertOnly();
        uint64_t getFileSize();
        bool isLegalFilePath();
        virtual bool isParaValid(CosResult &cosResult);
        string getFormatPath();
        string toJsonString();
        void setSha(string& sha);
        string getSha();
        bool isBufferUpload();
        const char* getBuffer();
        uint64_t getBufferLen();  
        FileUploadReq(){};
    private:
        string FileUploadSliceData(FileUploadReq req, string& initRsp);
        string FileUploadSliceFinish(FileUploadReq& req, string& dataRspJson);
        bool isSliceSizeValid();
        bool isSliceSizeAndShaValid();
    private:
        string sha;
        string biz_attr;
        string srcPath;
        string dstPath;
        const char*  buffer;
        uint64_t bufferLen;
        int    slice_size;
        int    insertOnly;
};

}

#endif
