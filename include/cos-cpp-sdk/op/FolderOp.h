#ifndef FOLDER_OP_H
#define FOLDER_OP_H
#include "op/BaseOp.h"
#include "util/FileUtil.h"
#include "util/HttpUtil.h"
#include "util/CodecUtil.h"
#include "util/HttpSender.h"
#include "request/FolderCreateReq.h"
#include "request/FolderUpdateReq.h"
#include "request/FolderDeleteReq.h"
#include "request/FolderStatReq.h"
#include "request/FolderListReq.h"
#include "CosParams.h"
#include "CosConfig.h"
#include "CosSysConfig.h"
#include <string>

using std::string;
namespace qcloud_cos{

class FolderOp : public BaseOp {

    public:
        FolderOp(CosConfig& config) : BaseOp(config) {};
        ~FolderOp(){};
        string FolderCreate(FolderCreateReq& req);
        string FolderUpdate(FolderUpdateReq& req);
        string FolderDelete(FolderDeleteReq& req);
        string FolderStat(FolderStatReq& req);
        string FolderList(FolderListReq& req);

    private:
        FolderOp();
};

}


#endif
