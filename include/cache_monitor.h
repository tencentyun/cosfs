#ifndef CACHE_MONITOR_H
#define CACHE_MONITOR_H

#include "cache.h"
#include "cos_log.h"
#include "cos_defines.h"

class CacheMonitor
{
public:
    static void monitorCache();
    static bool isNeedCheck();
    static void* monitor_run(void*);

private:
    static time_t last_check_time; 
};

#endif
