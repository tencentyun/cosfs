/*
 * cosfs - FUSE-based file system backed by Tencent COS
 * fdpage used  to mange the cache file in local disk
 */
#ifndef FD_PAGE_H_
#define FD_PAGE_H_

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/file.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <string>
#include <iostream>
#include <sstream>
#include <map>
#include <list>
#include <vector>

class AutoLock1 {
  public:
    explicit AutoLock1(pthread_mutex_t* pmutex);
    ~AutoLock1();

  private:
    pthread_mutex_t* auto_mutex;
};

//------------------------------------------------
// fdpage & PageList
//------------------------------------------------
// page block information
// use fix-size block 1MB
const int kPageSize = 8 << 20; // 8MB
struct fdpage {
    char*  page_addr; // page在内存中的地址
    off_t  offset;    // 该page在文件中的偏移量
    size_t bytes;     // 该page中保存的文件的字节数
    int  ref_cnt;     // 该page的引用计数

    fdpage(char* start = NULL, off_t offset = 0, size_t bytes = 0)
           : page_addr(start), offset(offset), bytes(bytes), ref_cnt(0) {}

    size_t remain_size() const {
        return kPageSize - bytes;
    }

    void add_ref() {
        ref_cnt++;
    }

    void sub_ref() {
        if (ref_cnt > 0) ref_cnt--;
    }

    // 在文件中的下个偏移量
    size_t next() {
        return (size_t)offset + bytes;
    }

    size_t end() const {
        return offset + bytes - 1;
    }

    char* page_next() {
        if (page_addr) {
            return page_addr + bytes - 1;
        }
        return NULL;
    }

    char* page_end() const {
        if (page_addr) {
            return page_addr + kPageSize;
        }
        return NULL;
    }

    bool is_not_empty() const {
        return bytes > 0 && page_addr != NULL; 
    }

    char* cache_start(off_t size) const {
        if (offset <= size && size <= end()) {
            return page_addr + size - offset;
        }
        return NULL;
    }

    void clear() {
        offset = 0;
        bytes = 0;
        ref_cnt = 0;
        if (page_addr) {
            memset(page_addr, '\0', kPageSize);
        }
    }
};

typedef std::list<struct fdpage*> fdpage_list_t;

//
// Management of loading area/modifying
//
class PageList {
  public:
    static void FreeList(fdpage_list_t* list);

    explicit PageList(const std::string& file_cos_path, int32_t max_page_size = 10); // 默认只有10个page，每个page有2MB
    ~PageList();

    size_t GetUncachedPageSize(off_t start, size_t size);

    bool NeedLoadCachePage(off_t start, size_t size, off_t* new_start, fdpage** page);

    // 根据偏移量和需要的大小定位到缓存的page
    // 并且返回总共缓存的大小，有可能缓存的大小没有size那么大，那么有多大就返回多大
    size_t GetCachedPages(off_t start, size_t size, fdpage_list_t* list);

    ssize_t LoadAndOcuppyCachePages(off_t offset, size_t size, fdpage_list_t* list);

    size_t GetCachedContent(const fdpage_list_t* list,
            off_t start, size_t size, std::string* cached_content);

    void ReleasePages(fdpage_list_t* list);

    void InsertPageInOrder(fdpage* page);

    void AddRefCnt();

    void DecRefCnt();

    int GetRefCnt();

  private:
    void RetriveUnusedPages();

    fdpage_list_t m_pages;
    fdpage_list_t m_reclaimed_pages;
    int32_t m_max_page_num;
    int32_t m_allocated_page_num;

    std::string m_file_cos_path;
    int m_ref_cnt;

    pthread_mutex_t m_pagelist_lock;
};


class PagePool {
public:
    static PagePool* get() {
        return &m_singleton;   
    }

    explicit PagePool(int max_page_num = 500);

    ~PagePool();

    fdpage* GetPage();

    void ReleasePage(fdpage* page);
 
private:
    static PagePool m_singleton;
    static pthread_mutex_t page_lock;
    static bool            is_lock_init;

    fdpage_list_t m_pages;
    int m_max_page_num;
    int m_allocated_page_num;
};

typedef std::map<std::string, PageList*> FileCacheMap;
class FileCacheManager {
  public:
    static FileCacheManager* get() {
        return &m_singleton;   
    }

    FileCacheManager();
    ~FileCacheManager();

    PageList* Open(const std::string& cos_path);

    void Close(const std::string& cos_path);

  private:
    static FileCacheManager m_singleton;
    static pthread_mutex_t m_file_cacahe_lock;
    static bool            is_lock_init;
    static int s_max_cache_item_num;

    FileCacheMap m_file_cache;
    FileCacheMap m_idle_cache_list;
};
#endif // FD_CACHE_H_
