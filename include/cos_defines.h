#ifndef COS_DEFINES_H
#define COS_DEFINES_H
#include <string>
using std::string;
static const string CONFIG_APPID = "AppID";
static const string CONFIG_SECRET_ID= "SecretID";
static const string CONFIG_SECRET_KEY = "SecretKey";
static const string CONFIG_DOMAIN = "domain";
static const string COS_SDK_CONFIG_FILE = "cos_sdk_config_file";

static const string CONFIG_VALUE_COS = "cos";
static const string CONFIG_VALUE_CDN = "cdn";

static const string HTTP_HEAD = "http://";

static const string LAST_MODIFIED = "Last-Modified";
static const string CONTENT_LENGTH = "Content-Length";
static const string CONTENT_TYPE = "Content-Type";
static const string FILE_NAME = "name";
static const int    SIGN_EXPIRE_TIME = 300;
static const int    CACHE_VALID_TIME = 120;  //cache有效时长120秒
static const int    CACHE_EXPIRE_TIME = 180;  //cache有效时长3600秒
static const int    CACHE_SCAN_INTERVAL = 3600;  //cache扫描间隔

#endif

