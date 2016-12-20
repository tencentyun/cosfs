#ifndef COSFS_CACHE_H_
#define COSFS_CACHE_H_

#include "common.h"
#include <memory.h>
#include <sys/stat.h>
#include "cos_defines.h"
#include "cache_monitor.h"

struct stat_cache_entry {
  struct stat   stbuf;
  unsigned long ref_count;
  time_t        cache_date;
  string  key;
  headers_t     meta;

  stat_cache_entry() : ref_count(0), cache_date(0) {
    memset(&stbuf, 0, sizeof(struct stat));
    meta.clear();
  }
};

typedef std::map<std::string, stat_cache_entry*> stat_cache_t;

class StatCache
{
    friend class CacheMonitor;
  private:
    static StatCache       singleton;
    static pthread_mutex_t stat_cache_lock;
    stat_cache_t  stat_cache;
    bool          IsExpireTime;
    time_t        ExpireTime;
    unsigned long CacheSize;

  private:
    void Clear(void);
    bool GetStat(std::string& key, struct stat* pst, headers_t* meta, const char* petag, OP_FLAG flag = INVALID);
  public:
    StatCache();
    ~StatCache();
    static StatCache* getStatCacheData(void) {
      return &singleton;
    }
    unsigned long GetCacheSize(void) const;
    unsigned long SetCacheSize(unsigned long size);
    time_t GetExpireTime(void) const;
    time_t SetExpireTime(time_t expire);
    time_t UnsetExpireTime(void);
    bool isCacheExpire(stat_cache_entry *pStat);
    bool GetStat(std::string& key, OP_FLAG flag = INVALID) {
        return GetStat(key, NULL, NULL, NULL, flag);
    }
    bool GetStat(string& key, struct stat* stbuf){
        return GetStat(key, stbuf, NULL, NULL, INVALID);
    }
    bool AddStat(std::string& key, headers_t& meta);
    bool AddStatS(keys_maps& stats);
    bool DelStat(const char* key);
    bool DelStat(std::string& key) {
      return DelStat(key.c_str());
    }
};

bool convert_header_to_stat(const char* path, headers_t& meta, struct stat* pst);

#endif // COSFS_CACHE_H_
