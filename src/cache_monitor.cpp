#include <unistd.h>
#include <stdio.h>
#include <iostream>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include "cache_monitor.h"
#include "fdpage.h"

time_t CacheMonitor::last_check_time = time(NULL);

void* CacheMonitor::monitor_run(void*){
    while (true) {
        S3FS_PRN_INFO("monitorCache run begin");
        pthread_testcancel();
        monitorCache();
        pthread_testcancel();
        sleep(CACHE_SCAN_INTERVAL);

        S3FS_PRN_INFO("monitorCache run end");
    }

    return NULL;
}

void CacheMonitor::monitorCache()
{
    unsigned del_num = 0;
    StatCache* pStatCache = StatCache::getStatCacheData();
    S3FS_PRN_ERR("monitorCache scan begin.....,stat_cache.size=%lu",pStatCache->stat_cache.size());    
    AutoLock1 lock(&StatCache::stat_cache_lock);
    stat_cache_t::iterator iter = pStatCache->stat_cache.begin();
    while(iter != pStatCache->stat_cache.end())
    {
        stat_cache_entry *pEntry = iter->second;
        if (pStatCache->isCacheExpire(pEntry))
        {
            if (pEntry->ref_count == 0) {
                delete (*iter).second;
                pStatCache->stat_cache.erase(iter++);
                del_num++;
            } else {
                iter++;
            }
        }
        else 
        {
            iter++;
        }
    }

    S3FS_PRN_ERR("monitorCache scan end.....,stat_cache.size=%lu,del_num=%lu",pStatCache->stat_cache.size(),del_num);
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

