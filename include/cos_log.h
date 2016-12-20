#ifndef COS_LOG_H
#define COS_LOG_H
#include "common.h"
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <sys/types.h>


#define SAFESTRPTR(strptr) (strptr ? strptr : "")
#define IS_S3FS_LOG_ERR()    (S3FS_LOG_ERR  == (debug_level & S3FS_LOG_DBG))
#define IS_S3FS_LOG_WARN()   (S3FS_LOG_WARN == (debug_level & S3FS_LOG_DBG))
#define IS_S3FS_LOG_INFO()   (S3FS_LOG_INFO == (debug_level & S3FS_LOG_DBG))
#define IS_S3FS_LOG_DBG()    (S3FS_LOG_DBG  == (debug_level & S3FS_LOG_DBG))

#define S3FS_LOG_LEVEL_TO_SYSLOG(level) \
        ( S3FS_LOG_DBG  == (level & S3FS_LOG_DBG) ? LOG_DEBUG   : \
          S3FS_LOG_INFO == (level & S3FS_LOG_DBG) ? LOG_INFO    : \
          S3FS_LOG_WARN == (level & S3FS_LOG_DBG) ? LOG_WARNING : \
          S3FS_LOG_ERR  == (level & S3FS_LOG_DBG) ? LOG_ERR     : LOG_CRIT )

#define S3FS_LOG_LEVEL_STRING(level) \
        ( S3FS_LOG_DBG  == (level & S3FS_LOG_DBG) ? "[DBG] " : \
          S3FS_LOG_INFO == (level & S3FS_LOG_DBG) ? "[INF] " : \
          S3FS_LOG_WARN == (level & S3FS_LOG_DBG) ? "[WAN] " : \
          S3FS_LOG_ERR  == (level & S3FS_LOG_DBG) ? "[ERR] " : "[CRT] " )

#define S3FS_LOG_NEST_MAX    4
#define S3FS_LOG_NEST(nest)  (nest < S3FS_LOG_NEST_MAX ? s3fs_log_nest[nest] : s3fs_log_nest[S3FS_LOG_NEST_MAX - 1])

#define S3FS_LOW_LOGPRN(level, fmt, ...) \
       if(S3FS_LOG_CRIT == level || (S3FS_LOG_CRIT != debug_level && level == (debug_level & level))){ \
         if(foreground){ \
           fprintf(stdout, "%s%s:%s(%d): " fmt "%s\n", S3FS_LOG_LEVEL_STRING(level), __FILE__, __func__, __LINE__, __VA_ARGS__); \
         }else { \
           syslog(S3FS_LOG_LEVEL_TO_SYSLOG(level), "%s:%s(%d): " fmt "%s", __FILE__, __func__, __LINE__, __VA_ARGS__); \
         } \
       }

#define S3FS_LOW_LOGPRN2(level, nest, fmt, ...) \
       if(S3FS_LOG_CRIT == level || (S3FS_LOG_CRIT != debug_level && level == (debug_level & level))){ \
         if(foreground){ \
           fprintf(stdout, "%s%s%s:%s(%d): " fmt "%s\n", S3FS_LOG_LEVEL_STRING(level), S3FS_LOG_NEST(nest), __FILE__, __func__, __LINE__, __VA_ARGS__); \
         }else { \
	       syslog(S3FS_LOG_LEVEL_TO_SYSLOG(level), "%s %s(%d)" fmt "%s", S3FS_LOG_NEST(nest), __FILE__, __LINE__, __VA_ARGS__); \
         } \
       }

#define S3FS_LOW_LOGPRN_EXIT(fmt, ...) \
       if(foreground){ \
         fprintf(stderr, "cosfs: " fmt "%s\n", __VA_ARGS__); \
       }else { \
         fprintf(stderr, "cosfs: " fmt "%s\n", __VA_ARGS__); \
         syslog(S3FS_LOG_LEVEL_TO_SYSLOG(S3FS_LOG_CRIT), "cosfs: " fmt "%s", __VA_ARGS__); \
       }

#define S3FS_PRN_EXIT(fmt, ...)   S3FS_LOW_LOGPRN_EXIT(fmt, ##__VA_ARGS__, "")
#define S3FS_PRN_CRIT(fmt, ...)   S3FS_LOW_LOGPRN(S3FS_LOG_CRIT, fmt, ##__VA_ARGS__, "")
#define S3FS_PRN_ERR(fmt, ...)    S3FS_LOW_LOGPRN(S3FS_LOG_ERR,  fmt, ##__VA_ARGS__, "")
#define S3FS_PRN_WARN(fmt, ...)   S3FS_LOW_LOGPRN(S3FS_LOG_WARN, fmt, ##__VA_ARGS__, "")
#define S3FS_PRN_DBG(fmt, ...)    S3FS_LOW_LOGPRN(S3FS_LOG_DBG,  fmt, ##__VA_ARGS__, "")
#define S3FS_PRN_INFO(fmt, ...)   S3FS_LOW_LOGPRN2(S3FS_LOG_INFO, 0, fmt, ##__VA_ARGS__, "")
#define S3FS_PRN_INFO0(fmt, ...)  S3FS_LOG_INFO(fmt, __VA_ARGS__)
#define S3FS_PRN_INFO1(fmt, ...)  S3FS_LOW_LOGPRN2(S3FS_LOG_INFO, 1, fmt, ##__VA_ARGS__, "")
#define S3FS_PRN_INFO2(fmt, ...)  S3FS_LOW_LOGPRN2(S3FS_LOG_INFO, 2, fmt, ##__VA_ARGS__, "")
#define S3FS_PRN_INFO3(fmt, ...)  S3FS_LOW_LOGPRN2(S3FS_LOG_INFO, 3, fmt, ##__VA_ARGS__, "")
#define S3FS_PRN_CURL(fmt, ...)   S3FS_LOW_LOGPRN2(S3FS_LOG_CRIT, 0, fmt, ##__VA_ARGS__, "")

s3fs_log_level set_s3fs_log_level(s3fs_log_level level);
s3fs_log_level bumpup_s3fs_log_level(void);

#endif
