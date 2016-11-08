#ifndef FILE_REQ_H
#define FILE_REQ_H
#include "request/ReqBase.h"

namespace qcloud_cos{

class FileReq : public ReqBase
{
    public:
        FileReq(){};
        ~FileReq(){};
};

}

#endif

