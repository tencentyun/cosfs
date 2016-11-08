#ifndef REQ_BASE_H
#define REQ_BASE_H
#include "CosResult.h"
#include "CosDefines.h"
#include "CosParams.h"
#include <iostream>
#include <string>
using std::string;
namespace qcloud_cos{

class ReqBase
{
public:
    ReqBase(){};
    ReqBase(const string& bucket):msBucket(bucket){};
    virtual ~ReqBase(){};
    virtual bool isLegalFilePath() {return true;}
    virtual bool isParaValid(CosResult &cosResult) {return true;}
    virtual string toJsonString(){ return "";}
    string getBucket();

protected:
    string msBucket;
};

}
#endif
