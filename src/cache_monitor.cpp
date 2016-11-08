#include <unistd.h>
#include <stdio.h>
#include <iostream>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include "cache_monitor.h"

time_t CacheMonitor::last_check_time = time(NULL);

void CacheMonitor::monitorCache()
{
    StatCache* pStatCache = StatCache::getStatCacheData();
    S3FS_PRN_INFO("monitorCache scan begin.....,stat_cache.size=%d",pStatCache->stat_cache.size());
    stat_cache_t::iterator iter = pStatCache->stat_cache.begin();
    while(iter != pStatCache->stat_cache.end())
    {
        stat_cache_entry *pEntry = iter->second;
        if (pStatCache->isCacheExpire(pEntry))
        {
            S3FS_PRN_INFO("monitorCache, cache file %s expire",pEntry->meta[FILE_NAME].c_str());
            pthread_mutex_lock(&StatCache::stat_cache_lock);
            if (pEntry->ref_count == 0)
            {
                S3FS_PRN_INFO("monitorCache, cache file %s expire, delete",pEntry->meta[FILE_NAME].c_str());
                delete (*iter).second;
                pStatCache->stat_cache.erase(iter++);
            }
            pthread_mutex_unlock(&StatCache::stat_cache_lock);
        }
        else 
        {
            iter++;
        }
    }

    S3FS_PRN_INFO("monitorCache scan end.....,stat_cache.size=%d",pStatCache->stat_cache.size());
}

bool CacheMonitor::isNeedCheck()
{
    time_t now = time(NULL);        
    if (now - last_check_time >= CACHE_SCAN_INTERVAL)
    {
        last_check_time = now;
        return true;
    }
    
    return false;
}

