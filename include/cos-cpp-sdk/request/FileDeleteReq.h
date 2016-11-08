#ifndef FILE_DELETE_REQ_H
#define FILE_DELETE_REQ_H
#include "ReqBase.h"
#include <string>
using std::string;
namespace qcloud_cos{

class FileDeleteReq : public ReqBase{

public:
    FileDeleteReq(string& bucket, string& path) : ReqBase(bucket){
        this->path = path;
    }

    virtual bool isParaValid(CosResult &cosResult);
    string getFormatPath();    
    string toJsonString();
    string getFilePath();

private:
    FileDeleteReq();
private:
    string path;
};


}


#endif
