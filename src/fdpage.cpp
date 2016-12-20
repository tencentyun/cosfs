/*
 * cosfs - FUSE-based file system backed by Tencent COS
 *
 */

#include "fdpage.h"

#include "cos_log.h"
#include "cos_info.h"
using namespace std;

//-------------------------------------------------------------------
// Class AutoLock1
//-------------------------------------------------------------------
AutoLock1::AutoLock1(pthread_mutex_t* pmutex) : auto_mutex(pmutex) {
    pthread_mutex_lock(auto_mutex);
}

AutoLock1::~AutoLock1() {
    pthread_mutex_unlock(auto_mutex);
}

//------------------------------------------------
// PageList methods
//------------------------------------------------
void PageList::FreeList(fdpage_list_t* list) {
    for(fdpage_list_t::iterator iter = list->begin(); iter != list->end(); iter = list->erase(iter)) {
        PagePool::get()->ReleasePage(*iter); // 将page放回到pool中
    }
    list->clear();
}

PageList::PageList(const std::string& file_cos_path, int32_t max_page_size) {
    m_max_page_num = max_page_size;
    m_allocated_page_num = 0;
    pthread_mutex_init(&m_pagelist_lock, NULL);
    m_file_cos_path = file_cos_path;
    m_ref_cnt = 0;
}

PageList::~PageList() {
    FreeList(&m_pages);
    FreeList(&m_reclaimed_pages);
    m_allocated_page_num = 0;
    pthread_mutex_destroy(&m_pagelist_lock);
    m_ref_cnt = 0;
}

size_t PageList::GetUncachedPageSize(off_t start, size_t size) {
    off_t start_tmp = start;
    size_t cached_size = 0;
    size_t next = start + size;
    for (fdpage_list_t::iterator iter = m_pages.begin(); iter != m_pages.end(); ++iter) {
        if ((*iter)->offset <= start_tmp && start_tmp <= (*iter)->end()) {
            S3FS_PRN_INFO("find one cached page offset: %ld, end: %ld", (*iter)->offset, (*iter)->end());
            if ((*iter)->next() < next) {
                cached_size += (*iter)->next() - start_tmp;
                start_tmp += (*iter)->next() - start_tmp;
                S3FS_PRN_INFO("continue to find cache, next_start: %ld", start_tmp);
            } else {
                cached_size += next - start_tmp;
                S3FS_PRN_INFO("reach the end %ld", next);
                break;  // 已经结束了
            } 
        }
    }

    return size - cached_size;    
}

// 由于size肯定是小于pagesize的，所以这里只分配一个页面用于缓存
bool PageList::NeedLoadCachePage(off_t start, size_t size, off_t* new_start, fdpage** page) {
    *page = NULL;
    // 先查看缓存的有没有
    size_t uncached_size = GetUncachedPageSize(start, size);
    S3FS_PRN_INFO("Get uncached page size %ld", uncached_size);
    if (uncached_size <= 0) {
        return false;
    }

    if (m_allocated_page_num >= m_max_page_num) {
        // 先整体清理一遍没有用的page
        RetriveUnusedPages();
    }
    // uncached size就是从不连续的起点开始load，之前load的都可以复用
    *new_start = start + size - uncached_size;
    S3FS_PRN_INFO("need load cache start offset: %ld", *new_start);
    size_t end = start + size;
    // 再次分配cache
    fdpage_list_t::iterator iter = m_reclaimed_pages.begin();
    if (iter != m_reclaimed_pages.end()) {
        *page = *iter;
        m_reclaimed_pages.erase(iter);
        S3FS_PRN_INFO("get a cache page from uncached pagelist");
        return true;
    }

    if (*page == NULL && m_allocated_page_num < m_max_page_num) {
        *page = PagePool::get()->GetPage();
        if (*page) {
            S3FS_PRN_INFO("get a cache page from pagepool");
            ++m_allocated_page_num;
            return true;
        }
    }
    S3FS_PRN_CRIT("No more cache pages from pagepool");
    return false;
}

// 从pagelist中选取出包含该范围的pages, 
// 可能出现不能全部包含的场景，但是一般不会出现
size_t PageList::GetCachedPages(off_t start, size_t size, fdpage_list_t* list) {
    off_t start_tmp = start;
    size_t next = start + size;
    size_t cached_size = 0;
    S3FS_PRN_INFO("GetCachedPages: start_tmp=%lu, next=%lu,p_pages.size=%u",start_tmp,next,m_pages.size());
    for (fdpage_list_t::iterator iter = m_pages.begin(); iter != m_pages.end(); ++iter) {
        if ((*iter)->offset <= start_tmp && start_tmp <= (*iter)->end()) {
            S3FS_PRN_INFO("find one cached page offset: %ld, end: %ld", (*iter)->offset, (*iter)->end());
            (*iter)->add_ref();
            list->push_back(*iter);
            S3FS_PRN_INFO("add page to pagelist outer parameter");
            if ((*iter)->next() < next) {
                cached_size += (*iter)->next() - start_tmp;
                start_tmp += (*iter)->next() - start_tmp;;
                S3FS_PRN_INFO("Continue to get cached page, next start offset=%ld,cached_size=%lu", start_tmp,cached_size);
            } else {
                cached_size += next - start_tmp;
                S3FS_PRN_INFO("Already get all cached page, so break search...,cached_size=%lu",cached_size);
                break;  // 已经结束了
            } 
        }
    }

    S3FS_PRN_INFO("all cached size %ld, need size %ld, offset: %ld", cached_size, size, start);
    return cached_size;
}

ssize_t PageList::LoadAndOcuppyCachePages(off_t offset, size_t size, fdpage_list_t* list) {
    off_t new_start = offset;
    AutoLock1 auto_lock(&m_pagelist_lock);
    S3FS_PRN_INFO("LoadAndOcuppy offset:%ld size %ld", offset, size);
    fdpage* page = NULL;
    if (NeedLoadCachePage(offset, size, &new_start, &page)) {
        int ret_code = 0;
        FileDownloadReq req(CosInfo::bucket, m_file_cos_path);
        size_t down_len = cos_client->FileDownload(req, page->page_addr, kPageSize, offset, &ret_code);
        if (ret_code != 0){
            S3FS_PRN_ERR("cosfs_read: fail,offset=%lu, ret_code=%d, rsp: %s", offset, ret_code, page->page_addr);
            PagePool::get()->ReleasePage(page);
            return -EIO;
        }

        page->offset = new_start;
        page->bytes = down_len;
        //memcpy(page->page_addr, rspbuf.c_str(), rspbuf.size());
        S3FS_PRN_INFO("load page: offset :%ld, end: %ld", page->offset, page->end());
        InsertPageInOrder(page);
    }

    size_t cached_size = GetCachedPages(offset, size, list);
    return cached_size;
}

// 从pagelist的页面中取出需要的缓存数据
size_t PageList::GetCachedContent(const fdpage_list_t* list, off_t start, size_t size, string* cached_content) {
    off_t start_tmp = start;
    size_t next = start + size;
    size_t cached_size = 0;
    for(fdpage_list_t::const_iterator iter = list->begin(); iter != list->end(); ++iter) {
        if ((*iter)->offset <= start_tmp && start_tmp <= (*iter)->end()) {
            S3FS_PRN_INFO("find one cached page offset: %ld, end: %ld", (*iter)->offset, (*iter)->end());
            if (next < (*iter)->next()) {
                cached_content->append((*iter)->cache_start(start_tmp), next - start_tmp);
                cached_size += next - start_tmp;
                S3FS_PRN_INFO("Already get all cached page, so break search...");
                break;
            } else {
                cached_content->append((*iter)->cache_start(start_tmp), (*iter)->next() - start_tmp);
                cached_size += (*iter)->next() - start_tmp;
                start_tmp += (*iter)->next() - start_tmp;
                S3FS_PRN_INFO("Continue to get cached page, next start offset %ld", start_tmp);
            }
        }
    }

    S3FS_PRN_INFO("all cached_size %ld, need size %ld, return content size %ld", cached_size, size, cached_content->size());
    return cached_size;
}

void PageList::ReleasePages(fdpage_list_t* list) {
    AutoLock1 auto_lock(&m_pagelist_lock);
    for (fdpage_list_t::iterator iter = list->begin(); iter != list->end(); ++iter) {
        (*iter)->sub_ref();
        S3FS_PRN_INFO("release page offset: %ld, end: %ld, ref_cnt: %d", (*iter)->offset, (*iter)->end(), (*iter)->ref_cnt);       
    }
}

void PageList::RetriveUnusedPages() {
    for(fdpage_list_t::iterator iter = m_pages.begin(); iter != m_pages.end();) {
        if ((*iter)->ref_cnt == 0) {
            S3FS_PRN_INFO("retrive one unused page offset: %ld, end: %ld", (*iter)->offset, (*iter)->end()); 
            (*iter)->clear();
            m_reclaimed_pages.push_back(*iter);
            iter = m_pages.erase(iter);
        } else {
            ++iter;
        }
    }
}

void PageList::InsertPageInOrder(fdpage* page) {
    fdpage_list_t::iterator iter = m_pages.begin();
    for(; iter != m_pages.end(); ++iter) {
        if (page->offset <= (*iter)->end() && page->offset >= (*iter)->offset) {
            break;
        }
    }

    if (iter != m_pages.end()) {
        // 怎么处理page内的内容有交叉的部分
        m_pages.insert(++iter, page);
    } else {
        m_pages.push_back(page);
    }
    S3FS_PRN_INFO("Insert one page offset: %ld, end: %ld", page->offset, page->end()); 
}

void PageList::AddRefCnt() {
    AutoLock1 auto_lock(&m_pagelist_lock);
    m_ref_cnt++;
}

void PageList::DecRefCnt() {
    AutoLock1 auto_lock(&m_pagelist_lock);
    m_ref_cnt--;
}

int PageList::GetRefCnt() {
    AutoLock1 auto_lock(&m_pagelist_lock);
    return m_ref_cnt;
}


// -------------------------------------------------------------------
// Class PagePool
// --------------------------------------------------------------------
PagePool PagePool::m_singleton;
pthread_mutex_t PagePool::page_lock;
bool PagePool::is_lock_init(false);

PagePool::PagePool(int max_page_num)
    : m_max_page_num(max_page_num),
      m_allocated_page_num(0) {
    if(this == PagePool::get()){
        try {
            pthread_mutex_init(&PagePool::page_lock, NULL);
            PagePool::is_lock_init = true;
        } catch(exception& e){
            PagePool::is_lock_init = false;
            S3FS_PRN_CRIT("failed to init mutex");
        }
    } else {
        assert(false);
    }
}


PagePool::~PagePool() {
    {
        AutoLock1 auto_lock(&page_lock);
        for (fdpage_list_t::iterator iter = m_pages.begin();
            iter != m_pages.end(); iter = m_pages.erase(iter)) {
            free((*iter)->page_addr);
            delete *iter;
        }
    }

    if (is_lock_init) {
        pthread_mutex_destroy(&page_lock);
    }
}

fdpage* PagePool::GetPage() {
    AutoLock1 auto_lock(&page_lock);
    fdpage* page = NULL;
    S3FS_PRN_INFO("Pagepool ready to get one page");
    if (!m_pages.empty()) {
        fdpage_list_t::iterator it = m_pages.begin();
        page = *it;
        m_pages.erase(it);
        S3FS_PRN_INFO("Get one page from unsued list");
        return page;
    }

    if (m_allocated_page_num < m_max_page_num) {
        page = new fdpage();
        page->page_addr = (char*)malloc(sizeof(char) * kPageSize);
        page->clear();
        m_allocated_page_num++;
        S3FS_PRN_INFO("allocated one new page, allocated_page_num: %d, max_page_num %d", m_allocated_page_num, m_max_page_num);
    }

    return page;
}

void PagePool::ReleasePage(fdpage* page) {
    AutoLock1 auto_lock(&page_lock);
    page->clear();
    m_pages.push_back(page);
    S3FS_PRN_INFO("Pagepool ready to release one page back to pagepool");
}


// ---------------------------------------------------------------
// Class     FileCacheManager methods
// ---------------------------------------------------------------
FileCacheManager FileCacheManager::m_singleton;
pthread_mutex_t FileCacheManager::m_file_cacahe_lock;
bool  FileCacheManager::is_lock_init(false);
int FileCacheManager::s_max_cache_item_num = 100; // 默认只缓存100个文件的内容


FileCacheManager::FileCacheManager() {
    if(this == FileCacheManager::get()){
        try {
            pthread_mutex_init(&FileCacheManager::m_file_cacahe_lock, NULL);
            FileCacheManager::is_lock_init = true;
        } catch(exception& e){
            FileCacheManager::is_lock_init = false;
            S3FS_PRN_CRIT("failed to init mutex");
        }
    } else {
        assert(false);
    }
}

FileCacheManager::~FileCacheManager() {
    {
        AutoLock1 auto_lock(&m_file_cacahe_lock);
        for (FileCacheMap::iterator it = m_file_cache.begin(); it != m_file_cache.end(); ++it) {
            delete it->second;
        }
        m_file_cache.clear();
    }

    if (is_lock_init) {
        pthread_mutex_destroy(&m_file_cacahe_lock);
    }
}

PageList* FileCacheManager::Open(const std::string& file_cos_path) {
    AutoLock1 auto_lock(&m_file_cacahe_lock);
    FileCacheMap::iterator it = m_file_cache.find(file_cos_path);
    PageList* pagelist = NULL;
    if (it == m_file_cache.end()) {
        S3FS_PRN_INFO("No cache for file %s, begin to searching idle list", file_cos_path.c_str());
        it = m_idle_cache_list.find(file_cos_path);
        if (it != m_idle_cache_list.end()) {
            pagelist = it->second;
            m_idle_cache_list.erase(it);
            S3FS_PRN_INFO("Found cache in idle list for file %s", file_cos_path.c_str());
        } else {
            if (!m_idle_cache_list.empty() &&
                m_idle_cache_list.size() + m_file_cache.size() >= s_max_cache_item_num) {
                it = m_idle_cache_list.begin();
                S3FS_PRN_INFO("too much cache item, so delete one idle ache item for file %s", it->first.c_str());
                delete it->second;
                m_idle_cache_list.erase(it);
            }
            pagelist = new PageList(file_cos_path);    
        }
        m_file_cache[file_cos_path] = pagelist;
    } else {
        pagelist = it->second;
        S3FS_PRN_INFO("Found cache for file %s", file_cos_path.c_str());
    }

    pagelist->AddRefCnt();
    return pagelist;
}

void FileCacheManager::Close(const std::string& file_cos_path) {
    AutoLock1 auto_lock(&m_file_cacahe_lock);
    FileCacheMap::iterator it = m_file_cache.find(file_cos_path);
    if (it == m_file_cache.end()) {
        S3FS_PRN_ERR("No cache found for file %s, no need to close", file_cos_path.c_str());
        return;
    }

    it->second->DecRefCnt();
    S3FS_PRN_INFO("Close cache for fiee %s, ref_cnt now is : %d", file_cos_path.c_str(), it->second->GetRefCnt());
    if (it->second->GetRefCnt() == 0) {
        if ((m_file_cache.size() + m_idle_cache_list.size() - 1) < s_max_cache_item_num) {
            m_idle_cache_list[it->first] = it->second;
            S3FS_PRN_INFO("Ref count equal to zero, put into idle list cache for file %s", file_cos_path.c_str());
        } else {
            S3FS_PRN_INFO("Ref count equal to zero, so delete this cache for file %s", file_cos_path.c_str());
            delete it->second;
        }
        
        m_file_cache.erase(it);
    }
}
