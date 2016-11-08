#ifndef COSFS_OSSFS_UTIL_H_
#define COSFS_OSSFS_UTIL_H_
#include "common.h"
#include <sstream>
//-------------------------------------------------------------------
// Typedef
//-------------------------------------------------------------------

class AutoLock
{
  private:
    pthread_mutex_t* auto_mutex;
    bool             is_locked;

  public:
    AutoLock(pthread_mutex_t* pmutex = NULL);
    ~AutoLock();

    bool Lock(void);
    bool Unlock(void);
};

//-------------------------------------------------------------------
// Functions
//-------------------------------------------------------------------
std::string get_realpath(const char *path);
std::string get_username(uid_t uid);
int is_uid_inculde_group(uid_t uid, gid_t gid);
std::string mydirname(std::string path);
std::string mybasename(std::string path);
int mkdirp(const std::string& path, mode_t mode);
time_t cvtIAMExpireStringToTime(const char* s);
time_t get_mtime(const char *s);
time_t get_mtime(headers_t& meta);
off_t get_size(const char *s);
off_t get_size(headers_t& meta);
mode_t get_mode(const char *s);
mode_t get_mode(headers_t& meta, const char* path = NULL);
uid_t get_uid(const char *s);
uid_t get_uid(headers_t& meta);
gid_t get_gid(const char *s);
gid_t get_gid(headers_t& meta);
blkcnt_t get_blocks(off_t size);
time_t get_lastmodified(const char* s);
time_t get_lastmodified(headers_t& meta);

template<typename T> 
std::string str(T value) {
  std::stringstream s;
  s << value;
  return s.str();
}


#endif // COSFS_OSSFS_UTIL_H_
