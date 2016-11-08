#ifndef FILE_MOVE_REQ_H
#define FILE_MOVE_REQ_H
#include "ReqBase.h"
#include <string>

using std::string;
namespace qcloud_cos{

class FileMoveReq : public ReqBase
{

public:
    FileMoveReq(string& bucket, string& srcPath, string& dstPath, bool overwrite = false) : ReqBase(bucket){
       this->srcPath = srcPath;
       this->dstPath = dstPath;
       this->overwrite = (overwrite == true ? 1 : 0);
    }

    virtual bool isParaValid(CosResult &cosResult);
    int getOverWrite();
    string getDstPath();
    string getFilePath();
    string getFormatPath();
    string toJsonString();
    
private:
    FileMoveReq();
private:
    string srcPath;
    string dstPath;
    int    overwrite;
};

}
#endif
