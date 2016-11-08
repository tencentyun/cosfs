#ifndef FOLDER_UPDATE_REQ_H
#define FOLDER_UPDATE_REQ_H
#include "ReqBase.h"
#include <string>

using std::string;
namespace qcloud_cos{

class FolderUpdateReq : public ReqBase {

public:
    FolderUpdateReq(string& bucket, string& cosPath, string& bizAttr) : ReqBase(bucket){
        this->path = cosPath;
        this->biz_attr = bizAttr;
    }

    virtual bool isParaValid(CosResult &cosResult);
    bool isLegalFolderPath();
    string getBizAttr();
    string getFolderPath();
    string getFormatFolderPath();
    string toJsonString();
private:
    string path;
    string biz_attr;
};

}

#endif
