#ifndef BASE_OP_H
#define BASE_OP_H

#include "CosConfig.h"
#include <string>
#include <stdint.h>

using std::string;
namespace qcloud_cos{

class BaseOp
{
    public:
        BaseOp(CosConfig& cosConfig) : config(cosConfig){};
        ~BaseOp(){};
        CosConfig& getCosConfig();
        uint64_t getAppid();
        string getSecretID();        
        string getSecretKey();
        string statBase(const string &bucket,const string &path);
        string delBase(const string &bucket,const string &path);

    protected:
        BaseOp(){};
        CosConfig config;
        string op;
};

}
#endif
