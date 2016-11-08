#ifndef COS_API_H
#define COS_API_H
#include <string>
#include <stdint.h>
#include "request/FileUploadReq.h"
#include "request/FileDownloadReq.h"
#include "request/FileStatReq.h"
#include "request/FileDeleteReq.h"
#include "request/FileUpdateReq.h"
#include "request/FolderCreateReq.h"
#include "request/FolderStatReq.h"
#include "request/FolderDeleteReq.h"
#include "request/FolderUpdateReq.h"
#include "request/FolderListReq.h"
#include "op/FileOp.h"
#include "op/FolderOp.h"
#include "CosDefines.h"
#include "CosConfig.h"
#include "CosSysConfig.h"
#include "util/SimpleMutex.h"

using std::string;
namespace qcloud_cos{

class CosAPI
{
    public:
        CosAPI(CosConfig& config);
        ~CosAPI();

        //文件上传
        string FileUpload(FileUploadReq& req);
        //文件上传(异步)(callback的入参需要客户端端进行释放)
        bool FileUploadAsyn(FileUploadReq& req, UploadCallback callback);
        //文件下载
        int FileDownload(FileDownloadReq& req, char* buffer, size_t bufLen, uint64_t offset, int* ret_code);
        //文件下载(异步)(callback的入参需要客户端端进行释放)
        bool FileDownloadAsyn(FileDownloadReq& req, char* buffer, size_t bufLen, DownloadCallback callback);        
        //文件查询
        string FileStat(FileStatReq& req);
        //文件删除
        string FileDelete(FileDeleteReq& req);
        //文件更新
        string FileUpdate(FileUpdateReq& req);
        //目录创建
        string FolderCreate(FolderCreateReq& req);
        //目录查询
        string FolderStat(FolderStatReq& req);
        //目录删除
        string FolderDelete(FolderDeleteReq& req);
        //目录更新
        string FolderUpdate(FolderUpdateReq& req);
        //目录列表
        string FolderList(FolderListReq& req);
    private:
        CosAPI();
        int COS_Init();
        void COS_UInit();

    private:
        FileOp fileOp;
        FolderOp folderOp;
        static SimpleMutex init_mutex;
        static int g_init;
        static int cos_obj_num;
};


}
#endif
