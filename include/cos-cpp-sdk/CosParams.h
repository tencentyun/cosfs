#ifndef COS_PARAMS_H
#define COS_PARAMS_H

namespace qcloud_cos{

const string HTTP_HEADER_AUTHORIZATION = "Authorization";


const string PARA_OP = "op";
const string PARA_FILE_CONTENT = "filecontent";
const string PARA_SHA = "sha";
const string PARA_BIZ_ATTR = "biz_attr";
const string PARA_INSERT_ONLY = "insertOnly";
const string PARA_SLICE_SIZE = "slice_size";
const string PARA_FILE_SIZE = "filesize";
const string PARA_UPLOAD_PARTS = "uploadparts";
const string PARA_SESSION = "session";
const string PARA_OFFSET = "offset";

const string PARA_FORBID = "forbid";
const string PARA_AUTHORITY = "authority";
const string PARA_FLAG = "flag";
const string PARA_CUSTOM_HEADERS = "custom_headers";
const string PARA_CACHE_CONTROL = "Cache-Control";
const string PARA_CONTENT_TYPE = "Content-Type";
const string PARA_CONTENT_DISPOSITION = "Content-Disposition";
const string PARA_CONTENT_LANGUAGE = "Content-Language";
const string PARA_CONTENT_ENCODING = "Content-Encoding";
const string PARA_X_COS_META_PREFIX = "x-cos-meta-";

const string PARA_MOVE_DST_FILEID = "dest_fileid";
const string PARA_MOVE_OVER_WRITE = "to_over_write";

const string PARA_LIST_NUM = "num";
const string PARA_LIST_FLAG = "list_flag";
const string PARA_LIST_CONTEXT = "context";

const string OP_CREATE = "create";
const string OP_UPDATE = "update";
const string OP_UPLOAD = "upload";
const string OP_DELETE = "delete";
const string OP_LIST   = "list";
const string OP_STAT   = "stat";
const string OP_MOVE   = "move";
const string OP_UPLOAD_SLICE_INIT   = "upload_slice_init";
const string OP_UPLOAD_SLICE_DATA   = "upload_slice_data";
const string OP_UPLOAD_SLICE_FINISH   = "upload_slice_finish";
const string OP_UPLOAD_SLICE_LIST   = "upload_slice_list";


const string PARA_ERROR_DESC = "parameter error";
const string NETWORK_ERROR_DESC = "network error";
const string LOCAL_FILE_NOT_EXIST_DESC = "local file not exist";
const string PARA_PATH_ILEAGEL = "path ileagel error";
const string CAN_NOT_OP_ROOT_PATH = "can not operator root folder";

enum CosRetCode
{
    PARA_ERROR_CODE = -1,
    NETWORK_ERROR_CODE = -2,
    LOCAL_FILE_NOT_EXIST_CODE = -3,
};

enum FIlE_UPDATE_FLAG
{
    FLAG_BIZ_ATTR = 0x01,
    FLAG_FORBID   = 0x02,
    FLAG_RESERVERD_ATTR = 0x04,
    FLAG_CUSTOM_HEADER = 0x08,
    FLAG_AUTHORITY = 0x10,
    FLAG_REAL_SHA = 0x20
};

}

#endif
