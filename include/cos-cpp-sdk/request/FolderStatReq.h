#ifndef FOLDER_STAT_REQ_H
#define FOLDER_STAT_REQ_H
#include "ReqBase.h"
#include <string>

using std::string;
namespace qcloud_cos{

class FolderStatReq : public ReqBase {

public:
    FolderStatReq(string& bucket, string& cosPath) : ReqBase(bucket){
        this->path = cosPath;
    }

    virtual bool isParaValid(CosResult &cosResult);
    bool isLegalFolderPath();
    string getFolderPath();
    string getFormatFolderPath();
    string toJsonString();

private:
    string path;
};

}

#endif
