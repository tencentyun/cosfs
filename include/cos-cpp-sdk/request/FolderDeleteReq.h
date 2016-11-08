#ifndef FOLDER_DELETE_REQ_H
#define FOLDER_DELETE_REQ_H
#include "ReqBase.h"
#include <string>

using std::string;
namespace qcloud_cos{

class FolderDeleteReq : public ReqBase {

public:
    FolderDeleteReq(string& bucket, string& cosPath) : ReqBase(bucket){
        this->path = cosPath;
    }

    virtual bool isParaValid(CosResult &cosResult);
    bool isLegalFolderPath();
    string getFormatFolderPath();
    string getFolderPath();
    string toJsonString();    
private:
    string path;
};

}

#endif
