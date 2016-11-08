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
    time_t now = time(NULL);
    if (now - pStat->cache_date >= CACHE_EXPIRE_TIME)
    {
        return true;
    }

    return false;
}

void StatCache::Clear(void)
{
    pthread_mutex_lock(&StatCache::stat_cache_lock);

    for(stat_cache_t::iterator iter = stat_cache.begin(); iter != stat_cache.end(); stat_cache.erase(iter++)){
        if((*iter).second){
            delete (*iter).second;
        }
    }

    pthread_mutex_unlock(&StatCache::stat_cache_lock);
}

int StatCache::GetRefCount(string& key)
{
    int count = -1;
    pthread_mutex_lock(&StatCache::stat_cache_lock);
    stat_cache_t::iterator iter = stat_cache.find(key.c_str());
    if(iter != stat_cache.end() && (*iter).second){
        stat_cache_entry* ent = (*iter).second;
        count = ent->ref_count;
    }
    pthread_mutex_unlock(&StatCache::stat_cache_lock);

    return count;
}

stat_cache_entry* StatCache::GetStat(string& key)
{
    string strpath = key;
    stat_cache_entry* ent = NULL;

    if (CacheMonitor::isNeedCheck())
    {
        CacheMonitor::monitorCache();
    }

    pthread_mutex_lock(&StatCache::stat_cache_lock);

    stat_cache_t::iterator iter = stat_cache.find(strpath.c_str());

    if(iter != stat_cache.end() && (*iter).second){
        ent = (*iter).second;
    }

    pthread_mutex_unlock(&StatCache::stat_cache_lock);
    return ent;
}


bool StatCache::GetStat(string& key, struct stat* pst, headers_t* meta, const char* petag, OP_FLAG flag)
{
    bool is_delete_cache = false;
    string strpath = key;

    if (CacheMonitor::isNeedCheck())
    {
        CacheMonitor::monitorCache();
    }

    pthread_mutex_lock(&StatCache::stat_cache_lock);
    stat_cache_t::iterator iter = stat_cache.find(strpath.c_str());

    if(iter != stat_cache.end() && (*iter).second){
        stat_cache_entry* ent = (*iter).second;
        if(!IsExpireTime|| (ent->cache_date + ExpireTime) >= time(NULL)){
            // hit without checking etag
            if(petag){
                string stretag = ent->meta["ETag"];
                if('\0' != petag[0] && 0 != strcmp(petag, stretag.c_str())){
                    is_delete_cache = true;
                }
            }

            if(is_delete_cache){
                // not hit by different ETag
                //DPRNNN("stat cache not hit by ETag[path=%s][time=%jd][hit count=%lu][ETag(%s)!=(%s)]",
                //strpath.c_str(), (intmax_t)(ent->cache_date), 0, petag ? petag : "null", ent->meta["ETag"].c_str());
            }else{
                // hit 
                //DPRNNN("stat cache hit [path=%s][time=%jd][hit count=%lu]", strpath.c_str(), (intmax_t)(ent->cache_date),0);

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
                pthread_mutex_unlock(&StatCache::stat_cache_lock);
                return true;
            }
        }else{
            // timeout
            is_delete_cache = true;
        }
    }
    pthread_mutex_unlock(&StatCache::stat_cache_lock);

    /*
       if(is_delete_cache){
       DelStat(strpath);
       }*/
    return false;
}

bool StatCache::isCacheValid(stat_cache_entry* pStat)
{
    time_t now = time(NULL);
    int diff = now - pStat->cache_date;
    if ( diff <= CACHE_VALID_TIME )
    {
        return true;
    }
    return false;
}

bool StatCache::AddStat(std::string& key, headers_t& meta)
{
    //DPRNNN("add stat cache entry[path=%s]", key.c_str());

    pthread_mutex_lock(&StatCache::stat_cache_lock);
    stat_cache_entry* ent = NULL;
    bool new_ent = false;
    stat_cache_t::iterator iter = stat_cache.find(key);
    if(stat_cache.end() != iter){
        ent = (*iter).second;
    }else{
        // make new
        ent = new stat_cache_entry();
        new_ent = true;
        ent->ref_count = 0;
    }


    if(!convert_header_to_stat(key.c_str(), meta, &(ent->stbuf))){
        if (new_ent) {
            delete ent;
        }
        return false;
    }
    ent->cache_date = time(NULL); // Set time.
    ent->meta.clear();
    //copy only some keys
    for(headers_t::iterator iter = meta.begin(); iter != meta.end(); ++iter){
        string tag   = (*iter).first;
        string value = (*iter).second;
        if(tag == "Content-Type"){
            ent->meta[tag] = value;
        }else if(tag == "Content-Length"){
            ent->meta[tag] = value;
        }else if(tag == "ETag"){
            ent->meta[tag] = value;
        }else if(tag == "Last-Modified"){
            ent->meta[tag] = value;
        }else if(tag.substr(0, 5) == "x-cos"){
            ent->meta[tag] = value;
        } else if(tag == "name"){
            ent->meta[tag] = value;
        } else{
            // Check for upper case
            transform(tag.begin(), tag.end(), tag.begin(), static_cast<int (*)(int)>(std::tolower));
            if(tag.substr(0, 5) == "x-cos"){
                ent->meta[tag] = value;
            }
        }
    }
    // add
    stat_cache[key] = ent;
    pthread_mutex_unlock(&StatCache::stat_cache_lock);

    return true;
}


bool StatCache::DelStat(const char* key)
{
    if(!key){
        return false;
    }
    //DPRNNN("delete stat cache entry[path=%s]", key);

    pthread_mutex_lock(&StatCache::stat_cache_lock);

    stat_cache_t::iterator iter;
    if(stat_cache.end() != (iter = stat_cache.find(string(key)))){
        stat_cache_entry* ent = (*iter).second;
        if(--ent->ref_count == 0){
            delete (*iter).second;
            stat_cache.erase(iter);
        }
    }
    pthread_mutex_unlock(&StatCache::stat_cache_lock);

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

