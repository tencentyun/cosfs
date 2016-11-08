#ifndef FOLDER_LIST_REQ_H
#define FOLDER_LIST_REQ_H
#include "ReqBase.h"
#include <string>

using std::string;
namespace qcloud_cos{

class FolderListReq : public ReqBase {

public:
    FolderListReq(string& bucket, string& cosPath, int listNum = 1000, bool listFlag = true, string context = "") : ReqBase(bucket){
        this->path = cosPath;
        this->num = listNum;
        this->flag = (listFlag == true ? 1 : 0);
        this->context = context;
    }

    virtual bool isParaValid(CosResult &cosResult);
    bool isLegalFolderPath();
    void setContext(string& context);
    string getFormatFolderPath();
    string getListPath();
    int getListFlag();
    int getListNum();
    string getListContext();
    string toJsonString();    
private:
    FolderListReq();
private:
    int num;
    int flag;
    string path;
    string context;

};

}

#endif
