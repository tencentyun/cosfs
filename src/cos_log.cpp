#include "common.h"
#include "cos_log.h"
s3fs_log_level set_s3fs_log_level(s3fs_log_level level)
{
  if(level == debug_level){
    return debug_level;
  }
  s3fs_log_level old = debug_level;
  debug_level        = level;
  setlogmask(LOG_UPTO(S3FS_LOG_LEVEL_TO_SYSLOG(debug_level)));
  S3FS_PRN_CRIT("change debug level from %s to %s", S3FS_LOG_LEVEL_STRING(old), S3FS_LOG_LEVEL_STRING(debug_level));
  return old;
}

s3fs_log_level bumpup_s3fs_log_level(void)
{
  s3fs_log_level old = debug_level;
  debug_level        = ( S3FS_LOG_CRIT == debug_level ? S3FS_LOG_ERR :
                         S3FS_LOG_ERR  == debug_level ? S3FS_LOG_WARN :
                         S3FS_LOG_WARN == debug_level ? S3FS_LOG_INFO :
                         S3FS_LOG_INFO == debug_level ? S3FS_LOG_DBG :
                         S3FS_LOG_CRIT );
  setlogmask(LOG_UPTO(S3FS_LOG_LEVEL_TO_SYSLOG(debug_level)));
  S3FS_PRN_CRIT("change debug level from %sto %s", S3FS_LOG_LEVEL_STRING(old), S3FS_LOG_LEVEL_STRING(debug_level));
  return old;
}

