#ifndef FILE_UPDATE_REQ_H
#define FILE_UPDATE_REQ_H
#include "ReqBase.h"
#include <string>
#include <map>

using std::string;
using std::map;
namespace qcloud_cos{

class FileUpdateReq : public ReqBase {

public:
    FileUpdateReq(string& bucket, string& path);
    void setBizAttr(string& bizAttr);
    void setAuthority(string& authority);
    void setForbid(int forbid);
    void setCustomHeader(map<string,string>& custom_header);   
    bool isLegalFilePath();
    virtual bool isParaValid(CosResult &cosResult);
    string getFormatPath();
    string getBizAttr();
    string getAuthority();
    int getForbid();
    int getFlag();
    string getFilePath();
    map<string,string>& getCustomHeaders();
    string toJsonString();
    bool isUpdateBizAttr();
    bool isUpdateForBid();
    bool isUpdateAuthority();
    bool isUpdateCustomHeader();
private:
    bool isAuthorityValid();
    bool isCustomHeader(const string& head);

private:
    string path;
    string biz_attr;
    string authority;
    int forbid;
    int flag;  //记录更新的bit位
    map<string,string> custom_headers;
};

}

#endif
