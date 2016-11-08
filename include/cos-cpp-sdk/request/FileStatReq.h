#ifndef FILE_STAT_REQ_H
#define FILE_STAT_REQ_H
#include "ReqBase.h"
#include <string>

using std::string;
namespace qcloud_cos{

class FileStatReq : public ReqBase{

public:
    FileStatReq(string& bucket, string& cosPath) : ReqBase(bucket){
        this->path = cosPath;
    }

    virtual bool isParaValid(CosResult &cosResult);
    string getFormatPath();
    string getFilePath();
    string toJsonString();

private:
    FileStatReq();
private:
    string path;
};


}


#endif
