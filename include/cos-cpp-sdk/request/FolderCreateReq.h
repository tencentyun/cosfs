#ifndef FOLDER_CREATE_REQ_H
#define FOLDER_CREATE_REQ_H
#include "ReqBase.h"
#include <string>

using std::string;

namespace qcloud_cos{

class FolderCreateReq : public ReqBase {

public:
    FolderCreateReq(string& bucket, string& cosPath, string bizAttr="") : ReqBase(bucket){
        this->path = cosPath;
        this->biz_attr = bizAttr;
    }

    virtual bool isParaValid(CosResult &cosResult);
    bool isLegalFolderPath();
    string getBizAttr();
    string getFormatFolderPath();
    string getFolderPath();
    string toJsonString();

private:
    string path;
    string biz_attr;
};

}

#endif

