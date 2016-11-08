#ifndef COSFS_COMMON_H_
#define COSFS_COMMON_H_

#ifndef FUSE_USE_VERSION
#define FUSE_USE_VERSION      26
#endif

#include "fuse/fuse.h"
#include <string>
#include <map>
#include "cos-cpp-sdk/CosApi.h"
using std::string;
using std::map;
using namespace qcloud_cos;

#if 0
//
// Macro
//
#define SAFESTRPTR(strptr) (strptr ? strptr : "")

// for debug
#define	FPRINT_NEST_SPACE_0  ""
#define	FPRINT_NEST_SPACE_1  "  "
#define	FPRINT_NEST_SPACE_2  "    "
#define	FPRINT_NEST_CHECK(NEST) \
        (0 == NEST ? FPRINT_NEST_SPACE_0 : 1 == NEST ? FPRINT_NEST_SPACE_1 : FPRINT_NEST_SPACE_2)

#define LOWFPRINT(NEST, ...) \
        printf("%s%s(%d): ", FPRINT_NEST_CHECK(NEST), __func__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \

#define FPRINT(NEST, ...) \
        if(foreground){ \
          LOWFPRINT(NEST, __VA_ARGS__); \
        }

#define FPRINT2(NEST, ...) \
        if(foreground2){ \
          LOWFPRINT(NEST, __VA_ARGS__); \
        }

#define LOWSYSLOGPRINT(LEVEL, ...) \
        syslog(LEVEL, __VA_ARGS__);

#define SYSLOGPRINT(LEVEL, ...) \
    if(LEVEL <= LOG_ERR || debug){          \
          LOWSYSLOGPRINT(LEVEL, __VA_ARGS__); \
        }

/*
        if(LEVEL <= LOG_CRIT || debug){ \
          LOWSYSLOGPRINT(LEVEL, __VA_ARGS__); \
        }
*/

#define DPRINT(LEVEL, NEST, ...) \
        FPRINT(NEST, __VA_ARGS__); \
        SYSLOGPRINT(LEVEL, __VA_ARGS__);

#define DPRINT2(LEVEL, ...) \
        FPRINT2(2, __VA_ARGS__); \
        SYSLOGPRINT(LEVEL, __VA_ARGS__);

// print debug message
#define FPRN(...)      FPRINT(0, __VA_ARGS__)
#define FPRNN(...)     FPRINT(1, __VA_ARGS__)
#define FPRNNN(...)    FPRINT(2, __VA_ARGS__)
#define FPRNINFO(...)  FPRINT2(2, __VA_ARGS__)

// print debug message with putting syslog
#define DPRNCRIT(...)  DPRINT(LOG_CRIT, 0, __VA_ARGS__)
#define DPRN(...)      DPRINT(LOG_ERR, 0, __VA_ARGS__)
#define DPRNN(...)     DPRINT(LOG_DEBUG, 1, __VA_ARGS__)
#define DPRNNN(...)    DPRINT(LOG_ERR, 2, __VA_ARGS__)
#define DPRNINFO(...)  DPRINT2(LOG_INFO, __VA_ARGS__)

#define NR_DIR_SIZE 4096

//
// Typedef
//
typedef std::map<std::string, std::string> headers_t;

//
// Global valiables
//
extern bool debug;
extern bool foreground;
extern bool foreground2;
extern bool nomultipart;
extern std::string program_name;
extern std::string service_path;
extern std::string host;
extern std::string bucket;
extern std::string mount_prefix;
#endif

enum s3fs_log_level{
    S3FS_LOG_CRIT = 0,          // LOG_CRIT
    S3FS_LOG_ERR  = 1,          // LOG_ERR
    S3FS_LOG_WARN = 3,          // LOG_WARNING
    S3FS_LOG_INFO = 7,          // LOG_INFO
    S3FS_LOG_DBG  = 15          // LOG_DEBUG
};

enum OP_FLAG {
    INVALID = 0,
    GET_ATTR = 1,
    OPEN_FILE = 2,
    READ_FILE = 3,
    CLOSE_FILE = 4
};

extern const int nonempty;
extern string program_name;
extern string bucket;
extern string mountpoint;
extern bool foreground;
extern CosAPI* cos_client;
extern bool foreground;
extern const char* s3fs_log_nest[4];
extern s3fs_log_level debug_level;

typedef std::map<std::string, std::string> headers_t;
#define STR2NCMP(str1, str2)  strncmp(str1, str2, strlen(str2))

#endif // COSFS_COMMON_H_
