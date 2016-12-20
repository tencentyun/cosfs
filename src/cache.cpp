/*
 * cosfs - FUSE-based file system backed by Tencent-COS 
 * Modified based on s3fs - FUSE-based file system backed by Amazon S3 by Randy Rizun <rrizun@gmail.com>
 *
 * Copyright 2016
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>
#include <syslog.h>
#include <string>
#include <map>
#include <algorithm>
#include <list>

#include "cache.h"
#include "cosfs_util.h"
#include "cos_defines.h"
#include "fdpage.h"
#include "cos_log.h"
using namespace std;

//-------------------------------------------------------------------
// Static
//-------------------------------------------------------------------
StatCache       StatCache::singleton;
pthread_mutex_t StatCache::stat_cache_lock;

//-------------------------------------------------------------------
// Constructor/Destructor
//-------------------------------------------------------------------
StatCache::StatCache() : IsExpireTime(false), ExpireTime(0), CacheSize(1000)
{
    if(this == StatCache::getStatCacheData()){
        stat_cache.clear();
        pthread_mutex_init(&(StatCache::stat_cache_lock), NULL);
    }else{
        assert(false);
    }
}

StatCache::~StatCache()
{
    if(this == StatCache::getStatCacheData()){
        Clear();
        pthread_mutex_destroy(&(StatCache::stat_cache_lock));
    }else{
        assert(false);
    }
}

//-------------------------------------------------------------------
// Methods
//-------------------------------------------------------------------
unsigned long StatCache::GetCacheSize(void) const
{
    return CacheSize;
}

unsigned long StatCache::SetCacheSize(unsigned long size)
{
    unsigned long old = CacheSize;
    CacheSize = size;
    return old;
}

time_t StatCache::GetExpireTime(void) const
{
    return (IsExpireTime ? ExpireTime : (-1));
}

time_t StatCache::SetExpireTime(time_t expire)
{
    time_t old   = ExpireTime;
    ExpireTime   = expire;
    IsExpireTime = true;
    return old;
}

time_t StatCache::UnsetExpireTime(void)
{
    time_t old   = IsExpireTime ? ExpireTime : (-1);
    ExpireTime   = 0;
    IsExpireTime = false;
    return old;
}

bool StatCache::isCacheExpire(stat_cache_entry *pStat)
{
    if (!pStat) {
        return false;
    }
    time_t now = time(NULL);
    if (now - pStat->cache_date >= CACHE_EXPIRE_TIME)
    {
        return true;
    }

    return false;
}

void StatCache::Clear(void)
{
    AutoLock1 lock(&StatCache::stat_cache_lock);
    for(stat_cache_t::iterator iter = stat_cache.begin(); iter != stat_cache.end(); stat_cache.erase(iter++)){
        if((*iter).second){
            delete (*iter).second;
        }
    }
}

bool StatCache::GetStat(string& key, struct stat* pst, headers_t* meta, const char* petag, OP_FLAG flag)
{
    string strpath = key;
    AutoLock1 lock(&StatCache::stat_cache_lock);
    stat_cache_t::iterator iter = stat_cache.find(strpath.c_str());
    if(iter != stat_cache.end() && (*iter).second){
        stat_cache_entry* ent = (*iter).second;
        if (isCacheExpire(ent)) {
            //S3FS_PRN_ERR("cosfs_open: path=%s cache is expire",key.c_str());
            return false;
        }
        
        if(pst!= NULL){
            *pst= ent->stbuf;
        }
        if(meta != NULL){
            *meta = ent->meta;
        }
        if (flag == OPEN_FILE)
        {
            ent->ref_count++;
        }

        ent->cache_date = time(NULL);
        return true;
    }
    return false;
}

bool StatCache::AddStat(std::string& key, headers_t& meta)
{
    S3FS_PRN_INFO("AddStat: key=%s add to cache",key.c_str());
    AutoLock1 lock(&StatCache::stat_cache_lock);
    stat_cache_entry* ent = NULL;
    bool new_ent = false;
    stat_cache_t::iterator iter = stat_cache.find(key);
    if(stat_cache.end() != iter){
        ent = (*iter).second;
    }else{
        ent = new stat_cache_entry();
        new_ent = true;
        ent->ref_count = 0;
    }

    if(!convert_header_to_stat(key.c_str(), meta, &(ent->stbuf))){
        if (new_ent) {
            delete ent;
        }
        S3FS_PRN_ERR("AddStat: key=%s add to cache fail",key.c_str());
        return false;
    }
    ent->key = key;
    ent->cache_date = time(NULL);
    stat_cache[key] = ent;
    
    return true;
}

bool StatCache::AddStatS(keys_maps& stats)
{
    S3FS_PRN_INFO("AddStatS: size = %lu",stats.size());
    AutoLock1 lock(&StatCache::stat_cache_lock);
    keys_maps::iterator iter = stats.begin();
    for (;iter != stats.end(); ++iter) {
        string key = iter->first;
        headers_t& meta = iter->second;
        stat_cache_entry* ent = NULL;
        bool new_ent = false;
        stat_cache_t::iterator iter = stat_cache.find(key);
        if(stat_cache.end() != iter){
            ent = (*iter).second;
        }else{
            ent = new stat_cache_entry();
            new_ent = true;
            ent->ref_count = 0;
        }

        if(!convert_header_to_stat(key.c_str(), meta, &(ent->stbuf))){
            if (new_ent) {
                delete ent;
            }
            S3FS_PRN_ERR("AddStatS: key=%s add to cache fail",key.c_str());            
            continue;
        }
        ent->key = key;
        ent->cache_date = time(NULL);
        stat_cache[key] = ent;
    }
    
    return true;
}


bool StatCache::DelStat(const char* key)
{
    if(!key){
        return false;
    }

    AutoLock1 lock(&StatCache::stat_cache_lock);
    stat_cache_t::iterator iter = stat_cache.find(string(key));
    if(stat_cache.end() != iter ){
        stat_cache_entry* ent = (*iter).second;
        if (!ent) { 
            return false;
        }
        ent->cache_date = time(NULL);
        if (ent->ref_count > 0) {
            ent->ref_count--;
        } else {
            ent->ref_count = 0;
        }
    }
    return true;
}

//-------------------------------------------------------------------
// Functions
//-------------------------------------------------------------------
bool convert_header_to_stat(const char* path, headers_t& meta, struct stat* pst)
{
    if(!path || !pst){
        return false;
    }
    memset(pst, 0, sizeof(struct stat));

    pst->st_nlink = 1; // see fuse FAQ

    // mode
    pst->st_mode = get_mode(meta, path);

    // blocks
    if(S_ISREG(pst->st_mode)){
        pst->st_blocks = get_blocks(pst->st_size);
    }

    // mtime
    pst->st_mtime = get_mtime(meta);

    // size
    pst->st_size = get_size(meta);

    // uid/gid
    pst->st_uid = get_uid(meta);
    pst->st_gid = get_gid(meta);

    return true;
}

