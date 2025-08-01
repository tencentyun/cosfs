/*
 * s3fs - FUSE-based file system backed by Tencent COS
 *
 * Copyright 2007-2008 Randy Rizun <rrizun@gmail.com>
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
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <syslog.h>
#include <pthread.h>
#include <assert.h>
#include <curl/curl.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxml/tree.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <algorithm>
#include <list>
#include <vector>

#include "common.h"
#include "curl.h"
#include "string_util.h"
#include "s3fs.h"
#include "s3fs_util.h"
#include "s3fs_auth.h"
#include "fdcache.h"

using namespace std;

static const std::string empty_payload_hash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

extern int check_for_cos_format(bool); // implented in s3fs.cpp
//-------------------------------------------------------------------
// Utilities
//-------------------------------------------------------------------
// [TODO]
// This function uses tempolary file, but should not use it.
// For not using it, we implement function in each auth file(openssl, nss. gnutls).
//
static bool make_md5_from_string(const char* pstr, string& md5)
{
  if(!pstr || '\0' == pstr[0]){
    S3FS_PRN_ERR("Parameter is wrong.");
    return false;
  }
  FILE* fp;
  if(NULL == (fp = FdManager::MakeTempFile())){
    S3FS_PRN_ERR("failed to open temporary file by errno(%d)", errno);
    return false;
  }
  size_t length = strlen(pstr);
  if(length != fwrite(pstr, sizeof(char), length, fp)){
    S3FS_PRN_ERR("failed to write temporary file by errno(%d)", errno);
    fclose(fp);
    return false;
  }
  int fd;
  if(0 != fflush(fp) || 0 != fseek(fp, 0L, SEEK_SET) || -1 == (fd = fileno(fp))){
    S3FS_PRN_ERR("Failed to make MD5.");
    fclose(fp);
    return false;
  }
  // base64 md5
  md5 = s3fs_get_content_md5(fd);
  if(0 == md5.length()){
    S3FS_PRN_ERR("Failed to make MD5.");
    fclose(fp);
    return false;
  }
  fclose(fp);
  return true;
}

struct curl_slist* curl_slist_remove(struct curl_slist* list, const char* key) {
  struct curl_slist* current = list;
  struct curl_slist* prev = NULL;

  // 遍历链表
  while (current != NULL) {
    if (strncmp(current->data, key, strlen(key)) == 0) {
      // 找到匹配节点
      if (prev == NULL) {
        // 删除头节点
        list = current->next;
      } else {
        // 删除中间或尾部节点
        prev->next = current->next;
      }
      // 释放当前节点内存
      free(current->data);
      free(current);
      break;
    }
    prev = current;
    current = current->next;
  }
  return list;
}

//-------------------------------------------------------------------
// Class BodyData
//-------------------------------------------------------------------
#define BODYDATA_RESIZE_APPEND_MIN  (1 * 1024)         // 1KB
#define BODYDATA_RESIZE_APPEND_MID  (1 * 1024 * 1024)  // 1MB
#define BODYDATA_RESIZE_APPEND_MAX  (10 * 1024 * 1024) // 10MB
#define	AJUST_BLOCK(bytes, block)   (((bytes / block) + ((bytes % block) ? 1 : 0)) * block)

bool BodyData::Resize(size_t addbytes)
{
  if(IsSafeSize(addbytes)){
    return true;
  }

  // New size
  size_t need_size = AJUST_BLOCK((lastpos + addbytes + 1) - bufsize, sizeof(off_t));

  if(BODYDATA_RESIZE_APPEND_MAX < bufsize){
    need_size = (BODYDATA_RESIZE_APPEND_MAX < need_size ? need_size : BODYDATA_RESIZE_APPEND_MAX);
  }else if(BODYDATA_RESIZE_APPEND_MID < bufsize){
    need_size = (BODYDATA_RESIZE_APPEND_MID < need_size ? need_size : BODYDATA_RESIZE_APPEND_MID);
  }else if(BODYDATA_RESIZE_APPEND_MIN < bufsize){
    need_size = ((bufsize * 2) < need_size ? need_size : (bufsize * 2));
  }else{
    need_size = (BODYDATA_RESIZE_APPEND_MIN < need_size ? need_size : BODYDATA_RESIZE_APPEND_MIN);
  }
  // realloc
  char* newtext;
  if(NULL == (newtext = (char*)realloc(text, (bufsize + need_size)))){
    S3FS_PRN_CRIT("not enough memory (realloc returned NULL)");
    free(text);
    text = NULL;
    return false;
  }
  text     = newtext;
  bufsize += need_size;

  return true;
}

void BodyData::Clear(void)
{
  if(text){
    free(text);
    text = NULL;
  }
  lastpos = 0;
  bufsize = 0;
}

bool BodyData::Append(void* ptr, size_t bytes)
{
  if(!ptr){
    return false;
  }
  if(0 == bytes){
    return true;
  }
  if(!Resize(bytes)){
    return false;
  }
  memcpy(&text[lastpos], ptr, bytes);
  lastpos += bytes;
  text[lastpos] = '\0';

  return true;
}

const char* BodyData::str(void) const
{
  static const char* strnull = "";
  if(!text){
    return strnull;
  }
  return text;
}

//-------------------------------------------------------------------
// Class CurlHandlerPool
//-------------------------------------------------------------------

bool CurlHandlerPool::Init()
{
  if (0 != pthread_mutex_init(&mLock, NULL)) {
    S3FS_PRN_ERR("Init curl handlers lock failed");
    return false;
  }

  mHandlers = new CURL*[mMaxHandlers](); // this will init the array to 0
  for (int i = 0; i < mMaxHandlers; ++i, ++mIndex) {
    mHandlers[i] = curl_easy_init();
    if (!mHandlers[i]) {
      S3FS_PRN_ERR("Init curl handlers pool failed");
      Destroy();
      return false;
    }
  }

  return true;
}

bool CurlHandlerPool::Destroy()
{
  assert(mIndex >= -1 && mIndex < mMaxHandlers);

  for (int i = 0; i <= mIndex; ++i) {
    curl_easy_cleanup(mHandlers[i]);
  }
  delete[] mHandlers;

  if (0 != pthread_mutex_destroy(&mLock)) {
    S3FS_PRN_ERR("Destroy curl handlers lock failed");
    return false;
  }

  return true;
}

CURL* CurlHandlerPool::GetHandler()
{
  CURL* h = NULL;

  assert(mIndex >= -1 && mIndex < mMaxHandlers);

  pthread_mutex_lock(&mLock);
  if (mIndex >= 0) {
    S3FS_PRN_DBG("Get handler from pool: %d", mIndex);
    h = mHandlers[mIndex--];
  }
  pthread_mutex_unlock(&mLock);

  if (!h) {
    S3FS_PRN_INFO("Pool empty: create new handler");
    h = curl_easy_init();
  }

  return h;
}

void CurlHandlerPool::ReturnHandler(CURL* h)
{
  bool needCleanup = true;

  assert(mIndex >= -1 && mIndex < mMaxHandlers);

  pthread_mutex_lock(&mLock);
  if (mIndex < mMaxHandlers - 1) {
    mHandlers[++mIndex] = h;
    needCleanup = false;
    S3FS_PRN_DBG("Return handler to pool: %d", mIndex);
  }
  pthread_mutex_unlock(&mLock);

  if (needCleanup) {
    S3FS_PRN_INFO("Pool full: destroy the handler");
    curl_easy_cleanup(h);
  }
}

//-------------------------------------------------------------------
// Class S3fsCurl
//-------------------------------------------------------------------
#define MULTIPART_SIZE              10485760          // 10MB
#define MAX_MULTI_COPY_SOURCE_SIZE  524288000         // 500MB

#define	RAM_EXPIRE_MERGIN           (10 * 60)         // update timming
#define	RAM_CRED_URL                ""
#define RAMCRED_ACCESSKEYID         "TmpSecretId"
#define RAMCRED_SECRETACCESSKEY     "TmpSecretKey"
#define RAMCRED_ACCESSTOKEN         "Token"
#define RAMCRED_EXPIRATION          "Expiration"
#define RAMCRED_KEYCOUNT            4

// [NOTICE]
// This symbol is for libcurl under 7.23.0
#ifndef CURLSHE_NOT_BUILT_IN
#define CURLSHE_NOT_BUILT_IN        5
#endif

pthread_mutex_t  S3fsCurl::curl_handles_lock;
pthread_mutex_t  S3fsCurl::curl_share_lock[SHARE_MUTEX_MAX];
bool             S3fsCurl::is_initglobal_done  = false;
CurlHandlerPool* S3fsCurl::sCurlPool           = NULL;
int              S3fsCurl::sCurlPoolSize       = 32;
CURLSH*          S3fsCurl::hCurlShare          = NULL;
bool             S3fsCurl::is_cert_check       = true; // default
bool             S3fsCurl::is_dns_cache        = true; // default
bool             S3fsCurl::is_ssl_session_cache= true; // default
long             S3fsCurl::connect_timeout     = 300;  // default
time_t           S3fsCurl::readwrite_timeout   = 60;   // default
int              S3fsCurl::retries             = 3;    // default
bool             S3fsCurl::is_public_bucket    = false;
string           S3fsCurl::default_acl         = "public-read";
storage_class_t  S3fsCurl::storage_class       = STANDARD;
sseckeylist_t    S3fsCurl::sseckeys;
std::string      S3fsCurl::ssekmsid            = "";
sse_type_t       S3fsCurl::ssetype             = SSE_DISABLE;
bool             S3fsCurl::is_content_md5      = true;
bool             S3fsCurl::is_verbose          = false;
pthread_mutex_t  S3fsCurl::token_lock;
string           S3fsCurl::COSAccessKeyId;
string           S3fsCurl::COSSecretAccessKey;
string           S3fsCurl::COSAccessToken;
time_t           S3fsCurl::COSAccessTokenExpire= 0;
string           S3fsCurl::CAM_role;
string           S3fsCurl::CAM_role_url        = RAM_CRED_URL;
long             S3fsCurl::ssl_verify_hostname = 1;    // default(original code...)
curltime_t       S3fsCurl::curl_times;
curlprogress_t   S3fsCurl::curl_progress;
string           S3fsCurl::curl_ca_bundle;
mimes_t          S3fsCurl::mimeTypes;
int              S3fsCurl::max_parallel_cnt    = 10;              // default
off_t            S3fsCurl::multipart_size      = MULTIPART_SIZE; // default
bool             S3fsCurl::is_sigv4            = true;           // default
string           S3fsCurl::skUserAgent = "tencentyun-cosfs-v5-" + string(VERSION);
bool             S3fsCurl::is_client_info_in_delete = false;           // default

//-------------------------------------------------------------------
// Class methods for S3fsCurl
//-------------------------------------------------------------------
bool S3fsCurl::InitS3fsCurl(const char* MimeFile)
{
  if(0 != pthread_mutex_init(&S3fsCurl::curl_handles_lock, NULL)){
    return false;
  }
  if(0 != pthread_mutex_init(&S3fsCurl::curl_share_lock[SHARE_MUTEX_DNS], NULL)){
    return false;
  }
  if(0 != pthread_mutex_init(&S3fsCurl::curl_share_lock[SHARE_MUTEX_SSL_SESSION], NULL)){
    return false;
  }
  if(!S3fsCurl::InitMimeType(MimeFile)){
    return false;
  }
  if(!S3fsCurl::InitGlobalCurl()){
    return false;
  }
  sCurlPool = new CurlHandlerPool(sCurlPoolSize);
  if (!sCurlPool->Init()) {
    return false;
  }
  if(!S3fsCurl::InitShareCurl()){
    return false;
  }
  if(!S3fsCurl::InitCryptMutex()){
    return false;
  }
  if(0 != pthread_mutex_init(&S3fsCurl::token_lock, NULL)){
    return false;
  }
  return true;
}

bool S3fsCurl::DestroyS3fsCurl(void)
{
  int result = true;

  if(!S3fsCurl::DestroyCryptMutex()){
    result = false;
  }
  if(!S3fsCurl::DestroyShareCurl()){
    result = false;
  }
  if (!sCurlPool->Destroy()) {
    result = false;
  }
  if(!S3fsCurl::DestroyGlobalCurl()){
    result = false;
  }
  if(0 != pthread_mutex_destroy(&S3fsCurl::curl_share_lock[SHARE_MUTEX_DNS])){
    result = false;
  }
  if(0 != pthread_mutex_destroy(&S3fsCurl::curl_share_lock[SHARE_MUTEX_SSL_SESSION])){
    result = false;
  }
  if(0 != pthread_mutex_destroy(&S3fsCurl::curl_handles_lock)){
    result = false;
  }
  return result;
}

bool S3fsCurl::InitGlobalCurl(void)
{
  if(S3fsCurl::is_initglobal_done){
    return false;
  }
  if(CURLE_OK != curl_global_init(CURL_GLOBAL_ALL)){
    S3FS_PRN_ERR("init_curl_global_all returns error.");
    return false;
  }
  S3fsCurl::is_initglobal_done = true;
  return true;
}

bool S3fsCurl::DestroyGlobalCurl(void)
{
  if(!S3fsCurl::is_initglobal_done){
    return false;
  }
  curl_global_cleanup();
  S3fsCurl::is_initglobal_done = false;
  return true;
}

bool S3fsCurl::InitShareCurl(void)
{
  CURLSHcode nSHCode;

  if(!S3fsCurl::is_dns_cache && !S3fsCurl::is_ssl_session_cache){
    S3FS_PRN_INFO("Curl does not share DNS data.");
    return true;
  }
  if(S3fsCurl::hCurlShare){
    S3FS_PRN_WARN("already initiated.");
    return false;
  }
  if(NULL == (S3fsCurl::hCurlShare = curl_share_init())){
    S3FS_PRN_ERR("curl_share_init failed");
    return false;
  }
  if(CURLSHE_OK != (nSHCode = curl_share_setopt(S3fsCurl::hCurlShare, CURLSHOPT_LOCKFUNC, S3fsCurl::LockCurlShare))){
    S3FS_PRN_ERR("curl_share_setopt(LOCKFUNC) returns %d(%s)", nSHCode, curl_share_strerror(nSHCode));
    return false;
  }
  if(CURLSHE_OK != (nSHCode = curl_share_setopt(S3fsCurl::hCurlShare, CURLSHOPT_UNLOCKFUNC, S3fsCurl::UnlockCurlShare))){
    S3FS_PRN_ERR("curl_share_setopt(UNLOCKFUNC) returns %d(%s)", nSHCode, curl_share_strerror(nSHCode));
    return false;
  }
  if(S3fsCurl::is_dns_cache){
    nSHCode = curl_share_setopt(S3fsCurl::hCurlShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    if(CURLSHE_OK != nSHCode && CURLSHE_BAD_OPTION != nSHCode && CURLSHE_NOT_BUILT_IN != nSHCode){
      S3FS_PRN_ERR("curl_share_setopt(DNS) returns %d(%s)", nSHCode, curl_share_strerror(nSHCode));
      return false;
    }else if(CURLSHE_BAD_OPTION == nSHCode || CURLSHE_NOT_BUILT_IN == nSHCode){
      S3FS_PRN_WARN("curl_share_setopt(DNS) returns %d(%s), but continue without shared dns data.", nSHCode, curl_share_strerror(nSHCode));
    }
  }
  if(S3fsCurl::is_ssl_session_cache){
    nSHCode = curl_share_setopt(S3fsCurl::hCurlShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
    if(CURLSHE_OK != nSHCode && CURLSHE_BAD_OPTION != nSHCode && CURLSHE_NOT_BUILT_IN != nSHCode){
      S3FS_PRN_ERR("curl_share_setopt(SSL SESSION) returns %d(%s)", nSHCode, curl_share_strerror(nSHCode));
      return false;
    }else if(CURLSHE_BAD_OPTION == nSHCode || CURLSHE_NOT_BUILT_IN == nSHCode){
      S3FS_PRN_WARN("curl_share_setopt(SSL SESSION) returns %d(%s), but continue without shared ssl session data.", nSHCode, curl_share_strerror(nSHCode));
    }
  }
  if(CURLSHE_OK != (nSHCode = curl_share_setopt(S3fsCurl::hCurlShare, CURLSHOPT_USERDATA, (void*)&S3fsCurl::curl_share_lock[0]))){
    S3FS_PRN_ERR("curl_share_setopt(USERDATA) returns %d(%s)", nSHCode, curl_share_strerror(nSHCode));
    return false;
  }
  return true;
}

bool S3fsCurl::DestroyShareCurl(void)
{
  if(!S3fsCurl::hCurlShare){
    if(!S3fsCurl::is_dns_cache && !S3fsCurl::is_ssl_session_cache){
      return true;
    }
    S3FS_PRN_WARN("already destroy share curl.");
    return false;
  }
  if(CURLSHE_OK != curl_share_cleanup(S3fsCurl::hCurlShare)){
    return false;
  }
  S3fsCurl::hCurlShare = NULL;
  return true;
}

void S3fsCurl::LockCurlShare(CURL* handle, curl_lock_data nLockData, curl_lock_access laccess, void* useptr)
{
  if(!hCurlShare){
    return;
  }
  pthread_mutex_t* lockmutex = static_cast<pthread_mutex_t*>(useptr);
  if(CURL_LOCK_DATA_DNS == nLockData){
    pthread_mutex_lock(&lockmutex[SHARE_MUTEX_DNS]);
  }else if(CURL_LOCK_DATA_SSL_SESSION == nLockData){
    pthread_mutex_lock(&lockmutex[SHARE_MUTEX_SSL_SESSION]);
  }
}

void S3fsCurl::UnlockCurlShare(CURL* handle, curl_lock_data nLockData, void* useptr)
{
  if(!hCurlShare){
    return;
  }
  pthread_mutex_t* lockmutex = static_cast<pthread_mutex_t*>(useptr);
  if(CURL_LOCK_DATA_DNS == nLockData){
    pthread_mutex_unlock(&lockmutex[SHARE_MUTEX_DNS]);
  }else if(CURL_LOCK_DATA_SSL_SESSION == nLockData){
    pthread_mutex_unlock(&lockmutex[SHARE_MUTEX_SSL_SESSION]);
  }
}

bool S3fsCurl::InitCryptMutex(void)
{
  return s3fs_init_crypt_mutex();
}

bool S3fsCurl::DestroyCryptMutex(void)
{
  return s3fs_destroy_crypt_mutex();
}

// homegrown timeout mechanism
int S3fsCurl::CurlProgress(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow)
{
  CURL* curl = static_cast<CURL*>(clientp);
  time_t now = time(0);
  progress_t p(dlnow, ulnow);

  pthread_mutex_lock(&S3fsCurl::curl_handles_lock);

  // any progress?
  if(p != S3fsCurl::curl_progress[curl]){
    // yes!
    S3fsCurl::curl_times[curl]    = now;
    S3fsCurl::curl_progress[curl] = p;
  }else{
    // timeout?
    if(now - S3fsCurl::curl_times[curl] > readwrite_timeout){
      pthread_mutex_unlock(&S3fsCurl::curl_handles_lock);
      S3FS_PRN_ERR("timeout now: %jd, curl_times[curl]: %jd, readwrite_timeout: %jd",
                      (intmax_t)now, (intmax_t)(S3fsCurl::curl_times[curl]), (intmax_t)readwrite_timeout);
      return CURLE_ABORTED_BY_CALLBACK;
    }
  }

  pthread_mutex_unlock(&S3fsCurl::curl_handles_lock);
  return 0;
}

bool S3fsCurl::InitMimeType(const char* MimeFile)
{
  if(!MimeFile){
    MimeFile = "/etc/mime.types";  // default
  }

  string line;
  ifstream MT(MimeFile);
  if(MT.good()){
    while(getline(MT, line)){
      if(line[0]=='#'){
        continue;
      }
      if(line.size() == 0){
        continue;
      }

      stringstream tmp(line);
      string mimeType;
      tmp >> mimeType;
      while(tmp){
        string ext;
        tmp >> ext;
        if(ext.size() == 0){
          continue;
        }
        S3fsCurl::mimeTypes[ext] = mimeType;
      }
    }
  }
  return true;
}

//
// @param s e.g., "index.html"
// @return e.g., "text/html"
//
string S3fsCurl::LookupMimeType(string name)
{
  string result("application/octet-stream");
  string::size_type last_pos = name.find_last_of('.');
  string::size_type first_pos = name.find_first_of('.');
  string prefix, ext, ext2;

  // No dots in name, just return
  if(last_pos == string::npos){
    return result;
  }
  // extract the last extension
  if(last_pos != string::npos){
    ext = name.substr(1+last_pos, string::npos);
  }
  if (last_pos != string::npos) {
     // one dot was found, now look for another
     if (first_pos != string::npos && first_pos < last_pos) {
        prefix = name.substr(0, last_pos);
        // Now get the second to last file extension
        string::size_type next_pos = prefix.find_last_of('.');
        if (next_pos != string::npos) {
           ext2 = prefix.substr(1+next_pos, string::npos);
        }
     }
  }

  // if we get here, then we have an extension (ext)
  mimes_t::const_iterator iter = S3fsCurl::mimeTypes.find(ext);
  // if the last extension matches a mimeType, then return
  // that mime type
  if (iter != S3fsCurl::mimeTypes.end()) {
    result = (*iter).second;
    return result;
  }

  // return with the default result if there isn't a second extension
  if(first_pos == last_pos){
     return result;
  }

  // Didn't find a mime-type for the first extension
  // Look for second extension in mimeTypes, return if found
  iter = S3fsCurl::mimeTypes.find(ext2);
  if (iter != S3fsCurl::mimeTypes.end()) {
     result = (*iter).second;
     return result;
  }

  // neither the last extension nor the second-to-last extension
  // matched a mimeType, return the default mime type
  return result;
}

bool S3fsCurl::LocateBundle(void)
{
    // See if environment variable CURL_CA_BUNDLE is set
    // if so, check it, if it is a good path, then set the
    // curl_ca_bundle variable to it
    if(S3fsCurl::curl_ca_bundle.empty()){
        char* CURL_CA_BUNDLE = getenv("CURL_CA_BUNDLE");
        if(CURL_CA_BUNDLE != NULL)  {
            // check for existence and readability of the file
            std::ifstream BF(CURL_CA_BUNDLE);
            if(!BF.good()){
                S3FS_PRN_ERR("%s: file specified by CURL_CA_BUNDLE environment variable is not readable", program_name.c_str());
                return false;
            }
            BF.close();
            S3fsCurl::curl_ca_bundle = CURL_CA_BUNDLE;
            return true;
        }
    }else{
        // Already set ca bundle variable
        return true;
    }

    // not set via environment variable, look in likely locations

    ///////////////////////////////////////////
    // following comment from curl's (7.21.2) acinclude.m4 file
    ///////////////////////////////////////////
    // dnl CURL_CHECK_CA_BUNDLE
    // dnl -------------------------------------------------
    // dnl Check if a default ca-bundle should be used
    // dnl
    // dnl regarding the paths this will scan:
    // dnl /etc/ssl/certs/ca-certificates.crt Debian systems
    // dnl /etc/pki/tls/certs/ca-bundle.crt Redhat and Mandriva
    // dnl /usr/share/ssl/certs/ca-bundle.crt old(er) Redhat
    // dnl /usr/local/share/certs/ca-root.crt FreeBSD
    // dnl /etc/ssl/cert.pem OpenBSD
    // dnl /etc/ssl/certs/ (ca path) SUSE
    ///////////////////////////////////////////
    // Within CURL the above path should have been checked
    // according to the OS. Thus, although we do not need
    // to check files here, we will only examine some files.
    //
    std::ifstream BF("/etc/pki/tls/certs/ca-bundle.crt");
    if(BF.good()){
        BF.close();
        S3fsCurl::curl_ca_bundle = "/etc/pki/tls/certs/ca-bundle.crt";
    }else{
        BF.open("/etc/ssl/certs/ca-certificates.crt");
        if(BF.good()){
            BF.close();
            S3fsCurl::curl_ca_bundle = "/etc/ssl/certs/ca-certificates.crt";
        }else{
            BF.open("/usr/share/ssl/certs/ca-bundle.crt");
            if(BF.good()){
                BF.close();
                S3fsCurl::curl_ca_bundle = "/usr/share/ssl/certs/ca-bundle.crt";
            }else{
                BF.open("/usr/local/share/certs/ca-root.crt");
                if(BF.good()){
                    BF.close();
                    S3fsCurl::curl_ca_bundle = "/usr/share/ssl/certs/ca-bundle.crt";
                }else{
                    S3FS_PRN_ERR("%s: /.../ca-bundle.crt is not readable", program_name.c_str());
                    return false;
                }
            }
        }
    }
    return true;
}

size_t S3fsCurl::WriteMemoryCallback(void* ptr, size_t blockSize, size_t numBlocks, void* data)
{
  BodyData* body  = static_cast<BodyData*>(data);

  if(!body->Append(ptr, blockSize, numBlocks)){
    S3FS_PRN_CRIT("BodyData.Append() returned false.");
    S3FS_FUSE_EXIT();
    return -1;
  }
  return (blockSize * numBlocks);
}

size_t S3fsCurl::ReadCallback(void* ptr, size_t size, size_t nmemb, void* userp)
{
  S3fsCurl* pCurl = reinterpret_cast<S3fsCurl*>(userp);

  if(1 > (size * nmemb)){
    return 0;
  }
  if(0 >= pCurl->postdata_remaining){
    return 0;
  }
  int copysize = std::min((int)(size * nmemb), pCurl->postdata_remaining);
  memcpy(ptr, pCurl->postdata, copysize);

  pCurl->postdata_remaining = (pCurl->postdata_remaining > copysize ? (pCurl->postdata_remaining - copysize) : 0);
  pCurl->postdata          += static_cast<size_t>(copysize);

  return copysize;
}

size_t S3fsCurl::HeaderCallback(void* data, size_t blockSize, size_t numBlocks, void* userPtr)
{
  headers_t* headers = reinterpret_cast<headers_t*>(userPtr);
  string header(reinterpret_cast<char*>(data), blockSize * numBlocks);
  string key;
  stringstream ss(header);

  if(getline(ss, key, ':')){
    // Force to lower, only "x-cos"
    string lkey = key;
    transform(lkey.begin(), lkey.end(), lkey.begin(), static_cast<int (*)(int)>(std::tolower));
    if(lkey.compare(0, 5, "x-cos") == 0){
      key = lkey;
    }
    string value;
    getline(ss, value);
    (*headers)[key] = trim(value);
  }
  return blockSize * numBlocks;
}

size_t S3fsCurl::UploadReadCallback(void* ptr, size_t size, size_t nmemb, void* userp)
{
  S3fsCurl* pCurl = reinterpret_cast<S3fsCurl*>(userp);

  if(1 > (size * nmemb)){
    return 0;
  }
  if(-1 == pCurl->partdata.fd || 0 >= pCurl->partdata.size){
    return 0;
  }
  // read size
  ssize_t copysize = (size * nmemb) < (size_t)pCurl->partdata.size ? (size * nmemb) : (size_t)pCurl->partdata.size;
  ssize_t readbytes;
  ssize_t totalread;
  // read and set
  for(totalread = 0, readbytes = 0; totalread < copysize; totalread += readbytes){
    readbytes = pread(pCurl->partdata.fd, &((char*)ptr)[totalread], (copysize - totalread), pCurl->partdata.startpos + totalread);
    if(0 == readbytes){
      // eof
      break;
    }else if(-1 == readbytes){
      // error
      S3FS_PRN_ERR("read file error(%d).", errno);
      return 0;
    }
  }
  pCurl->partdata.startpos += totalread;
  pCurl->partdata.size     -= totalread;

  return totalread;
}

size_t S3fsCurl::DownloadWriteCallback(void* ptr, size_t size, size_t nmemb, void* userp)
{
  S3fsCurl* pCurl = reinterpret_cast<S3fsCurl*>(userp);

  if(1 > (size * nmemb)){
    return 0;
  }
  if(-1 == pCurl->partdata.fd || 0 >= pCurl->partdata.size){
    return 0;
  }

  // write size
  ssize_t copysize = (size * nmemb) < (size_t)pCurl->partdata.size ? (size * nmemb) : (size_t)pCurl->partdata.size;
  ssize_t writebytes;
  ssize_t totalwrite;

  // write
  for(totalwrite = 0, writebytes = 0; totalwrite < copysize; totalwrite += writebytes){
    writebytes = pwrite(pCurl->partdata.fd, &((char*)ptr)[totalwrite], (copysize - totalwrite), pCurl->partdata.startpos + totalwrite);
    if(0 == writebytes){
      // eof?
      break;
    }else if(-1 == writebytes){
      // error
      S3FS_PRN_ERR("write file error(%d).", errno);
      return 0;
    }
  }
  pCurl->partdata.startpos += totalwrite;
  pCurl->partdata.size     -= totalwrite;

  return totalwrite;
}

bool S3fsCurl::SetCheckCertificate(bool isCertCheck) {
    bool old = S3fsCurl::is_cert_check;
    S3fsCurl::is_cert_check = isCertCheck;
    return old;
}

bool S3fsCurl::SetDnsCache(bool isCache)
{
  bool old = S3fsCurl::is_dns_cache;
  S3fsCurl::is_dns_cache = isCache;
  return old;
}

bool S3fsCurl::SetSslSessionCache(bool isCache)
{
  bool old = S3fsCurl::is_ssl_session_cache;
  S3fsCurl::is_ssl_session_cache = isCache;
  return old;
}

long S3fsCurl::SetConnectTimeout(long timeout)
{
  long old = S3fsCurl::connect_timeout;
  S3fsCurl::connect_timeout = timeout;
  return old;
}

time_t S3fsCurl::SetReadwriteTimeout(time_t timeout)
{
  time_t old = S3fsCurl::readwrite_timeout;
  S3fsCurl::readwrite_timeout = timeout;
  return old;
}

int S3fsCurl::SetRetries(int count)
{
  int old = S3fsCurl::retries;
  S3fsCurl::retries = count;
  return old;
}

bool S3fsCurl::SetPublicBucket(bool flag)
{
  bool old = S3fsCurl::is_public_bucket;
  S3fsCurl::is_public_bucket = flag;
  return old;
}

string S3fsCurl::SetDefaultAcl(const char* acl)
{
  string old = S3fsCurl::default_acl;
  S3fsCurl::default_acl = acl ? acl : "";
  return old;
}

storage_class_t S3fsCurl::SetStorageClass(storage_class_t storage_class)
{
  storage_class_t old = S3fsCurl::storage_class;
  S3fsCurl::storage_class = storage_class;
  return old;
}

bool S3fsCurl::PushbackSseKeys(string& onekey)
{
  onekey = trim(onekey);
  if(0 == onekey.size()){
    return false;
  }
  if('#' == onekey[0]){
    return false;
  }
  // make base64
  char* pbase64_key;
  if(NULL == (pbase64_key = s3fs_base64((unsigned char*)onekey.c_str(), onekey.length()))){
    S3FS_PRN_ERR("Failed to convert base64 from SSE-C key %s", onekey.c_str());
    return false;
  }
  string base64_key = pbase64_key;
  free(pbase64_key);

  // make MD5
  string strMd5;
  if(!make_md5_from_string(onekey.c_str(), strMd5)){
    S3FS_PRN_ERR("Could not make MD5 from SSE-C keys(%s).", onekey.c_str());
    return false;
  }
  // mapped MD5 = SSE Key
  sseckeymap_t md5map;
  md5map.clear();
  md5map[strMd5] = base64_key;
  S3fsCurl::sseckeys.push_back(md5map);
  return true;
}

sse_type_t S3fsCurl::SetSseType(sse_type_t type)
{
  sse_type_t    old = S3fsCurl::ssetype;
  S3fsCurl::ssetype = type;
  return old;
}

bool S3fsCurl::SetSseCKeys(const char* filepath)
{
  if(!filepath){
    S3FS_PRN_ERR("SSE-C keys filepath is empty.");
    return false;
  }
  struct stat st;
  if(0 != stat(filepath, &st)){
    S3FS_PRN_ERR("could not open use_sse keys file(%s).", filepath);
    return false;
  }
  if(st.st_mode & (S_IXUSR | S_IRWXG | S_IRWXO)){
    S3FS_PRN_ERR("use_sse keys file %s should be 0600 permissions.", filepath);
    return false;
  }

  S3fsCurl::sseckeys.clear();

  ifstream ssefs(filepath);
  if(!ssefs.good()){
    S3FS_PRN_ERR("Could not open SSE-C keys file(%s).", filepath);
    return false;
  }

  string   line;
  while(getline(ssefs, line)){
    S3fsCurl::PushbackSseKeys(line);
  }
  if(0 == S3fsCurl::sseckeys.size()){
    S3FS_PRN_ERR("There is no SSE Key in file(%s).", filepath);
    return false;
  }
  return true;
}

bool S3fsCurl::SetSseKmsid(const char* kmsid)
{
  if(!kmsid || '\0' == kmsid[0]){
    S3FS_PRN_ERR("SSE-KMS kms id is empty.");
    return false;
  }
  S3fsCurl::ssekmsid = kmsid;
  return true;
}

// [NOTE]
// Because SSE is set by some options and environment,
// this function check the integrity of the SSE data finally.
bool S3fsCurl::FinalCheckSse(void)
{
  if(SSE_DISABLE == S3fsCurl::ssetype){
    S3fsCurl::ssekmsid.erase();
  }else if(SSE_OSS == S3fsCurl::ssetype){
    S3fsCurl::ssekmsid.erase();
  }else if(SSE_C == S3fsCurl::ssetype){
    if(0 == S3fsCurl::sseckeys.size()){
      S3FS_PRN_ERR("sse type is SSE-C, but there is no custom key.");
      return false;
    }
    S3fsCurl::ssekmsid.erase();
  }else if(SSE_KMS == S3fsCurl::ssetype){
    if(S3fsCurl::ssekmsid.empty()){
      S3FS_PRN_ERR("sse type is SSE-KMS, but there is no specified kms id.");
      return false;
    }
    if(!S3fsCurl::IsSignatureV4()){
      S3FS_PRN_ERR("sse type is SSE-KMS, but signature type is not v4. SSE-KMS require signature v4.");
      return false;
    }
  }else{
    S3FS_PRN_ERR("sse type is unknown(%d).", S3fsCurl::ssetype);
    return false;
  }
  return true;
}

bool S3fsCurl::LoadEnvSseCKeys(void)
{
  char* envkeys = getenv("OSSSSECKEYS");
  if(NULL == envkeys){
    // nothing to do
    return true;
  }
  S3fsCurl::sseckeys.clear();

  istringstream fullkeys(envkeys);
  string        onekey;
  while(getline(fullkeys, onekey, ':')){
    S3fsCurl::PushbackSseKeys(onekey);
  }
  if(0 == S3fsCurl::sseckeys.size()){
    S3FS_PRN_ERR("There is no SSE Key in environment(OSSSSECKEYS=%s).", envkeys);
    return false;
  }
  return true;
}

bool S3fsCurl::LoadEnvSseKmsid(void)
{
  char* envkmsid = getenv("OSSSSEKMSID");
  if(NULL == envkmsid){
    // nothing to do
    return true;
  }
  return S3fsCurl::SetSseKmsid(envkmsid);
}

//
// If md5 is empty, returns first(current) sse key.
//
bool S3fsCurl::GetSseKey(string& md5, string& ssekey)
{
  for(sseckeylist_t::const_iterator iter = S3fsCurl::sseckeys.begin(); iter != S3fsCurl::sseckeys.end(); ++iter){
    if(0 == md5.length() || md5 == (*iter).begin()->first){
      md5    = iter->begin()->first;
      ssekey = iter->begin()->second;
      return true;
    }
  }
  return false;
}

bool S3fsCurl::GetSseKeyMd5(int pos, string& md5)
{
  if(pos < 0){
    return false;
  }
  if(S3fsCurl::sseckeys.size() <= static_cast<size_t>(pos)){
    return false;
  }
  int cnt = 0;
  for(sseckeylist_t::const_iterator iter = S3fsCurl::sseckeys.begin(); iter != S3fsCurl::sseckeys.end(); ++iter, ++cnt){
    if(pos == cnt){
      md5 = iter->begin()->first;
      return true;
    }
  }
  return false;
}

int S3fsCurl::GetSseKeyCount(void)
{
  return S3fsCurl::sseckeys.size();
}

bool S3fsCurl::SetContentMd5(bool flag)
{
  bool old = S3fsCurl::is_content_md5;
  S3fsCurl::is_content_md5 = flag;
  return old;
}

bool S3fsCurl::SetVerbose(bool flag)
{
  bool old = S3fsCurl::is_verbose;
  S3fsCurl::is_verbose = flag;
  return old;
}

bool S3fsCurl::checkSTSCredentialUpdate(void) {
    if (S3fsCurl::CAM_role.empty()) {
        return true;
    }

    {
        AutoLock auto_lock(&token_lock);
        if (time(NULL) + RAM_EXPIRE_MERGIN <= S3fsCurl::COSAccessTokenExpire) {
            return true;
        }
    }

   // if return value is not equal 1, means wrong format key
   if (check_for_cos_format(true) != 1) {
       return false;
   }

   return true;
}


bool S3fsCurl::SetToken(const string& token, const string& token_expire) {
    COSAccessToken = token;
    COSAccessTokenExpire = cvtCAMExpireStringToTime(token_expire.c_str());
    return true;
}

bool S3fsCurl::SetAccessKey(const char* AccessKeyId, const char* SecretAccessKey)
{
  if(!AccessKeyId || '\0' == AccessKeyId[0] || !SecretAccessKey || '\0' == SecretAccessKey[0]){
    return false;
  }
  COSAccessKeyId     = AccessKeyId;
  COSSecretAccessKey = SecretAccessKey;
  return true;
}

bool S3fsCurl::GetAccessKey(std::string &accessKey, std::string &secretKey)
{
  accessKey = COSAccessKeyId;
  secretKey = COSSecretAccessKey;
  return true;
}

// 自动更新秘钥
bool S3fsCurl::GetAccessKeyWithToken(std::string &accessKey, std::string &secretKey, std::string &token)
{
    AutoLock auto_lock(&token_lock);
    accessKey = COSAccessKeyId;
    secretKey = COSSecretAccessKey;
    token = COSAccessToken;
    return true;
}

bool S3fsCurl::SetAccessKeyWithToken(const std::string& accessKey, const std::string& secretKey, const std::string& token, const std::string& token_expired)
{
    AutoLock auto_lock(&token_lock);
    COSAccessKeyId = accessKey;
    COSSecretAccessKey = secretKey;
    COSAccessToken = token;
    COSAccessTokenExpire = cvtCAMExpireStringToTime(token_expired.c_str());
    return true;
}

long S3fsCurl::SetSslVerifyHostname(long value)
{
  if(0 != value && 1 != value){
    return -1;
  }
  long old = S3fsCurl::ssl_verify_hostname;
  S3fsCurl::ssl_verify_hostname = value;
  return old;
}

string S3fsCurl::SetCAMRole(const char* role)
{
  string old = S3fsCurl::CAM_role;
  S3fsCurl::CAM_role = role ? role : "";
  return old;
}

string S3fsCurl::SetCAMRoleUrl(const char* role_url)
{
  string old = S3fsCurl::CAM_role_url;
  S3fsCurl::CAM_role_url = role_url ? role_url : "";
  return old;
}

bool S3fsCurl::SetMultipartSize(off_t size)
{
  size = size * 1024 * 1024;
  if(size < MIN_MULTIPART_SIZE){
    return false;
  }
  S3fsCurl::multipart_size = size;
  return true;
}

int S3fsCurl::SetMaxParallelCount(int value)
{
  int old = S3fsCurl::max_parallel_cnt;
  S3fsCurl::max_parallel_cnt = value;
  return old;
}

bool S3fsCurl::UploadMultipartPostCallback(S3fsCurl* s3fscurl)
{
  if(!s3fscurl){
    return false;
  }
  // no check etag(md5);
  // XXX cos etag requires upper case.
  // if(NULL == strstr(s3fscurl->headdata->str(), upper(s3fscurl->partdata.etag).c_str())){
  //  return false;
  // }
  S3FS_PRN_ERR("headdata is : %s", s3fscurl->headdata->str());
  string header_str(s3fscurl->headdata->str(), s3fscurl->headdata->size());
  int pos = header_str.find("ETag: \"");
  if (pos != std::string::npos) {
      if (header_str.at(pos+39) != '"') {
          // sha1
          s3fscurl->partdata.etag = header_str.substr(pos + 7, 40);
      } else {
          s3fscurl->partdata.etag = header_str.substr(pos + 7, 32);  // ETag get md5 value
      }
      S3FS_PRN_ERR("partdata.etag : %s", s3fscurl->partdata.etag.c_str());
  }
  s3fscurl->partdata.etaglist->at(s3fscurl->partdata.etagpos).assign(s3fscurl->partdata.etag);
  s3fscurl->partdata.uploaded = true;

  return true;
}

S3fsCurl* S3fsCurl::UploadMultipartPostRetryCallback(S3fsCurl* s3fscurl)
{
  if(!s3fscurl){
    return NULL;
  }
  // parse and get part_num, upload_id.
  string upload_id;
  string part_num_str;
  int    part_num;
  if(!get_keyword_value(s3fscurl->url, "uploadId", upload_id)){
    return NULL;
  }
  if(!get_keyword_value(s3fscurl->url, "partNumber", part_num_str)){
    return NULL;
  }
  part_num = atoi(part_num_str.c_str());

  if(s3fscurl->retry_count >= S3fsCurl::retries){
    S3FS_PRN_ERR("Over retry count(%d) limit(%s:%d).", s3fscurl->retry_count, s3fscurl->path.c_str(), part_num);
    return NULL;
  }

  // duplicate request
  S3fsCurl* newcurl            = new S3fsCurl(s3fscurl->IsUseAhbe());
  newcurl->partdata.etaglist   = s3fscurl->partdata.etaglist;
  newcurl->partdata.etagpos    = s3fscurl->partdata.etagpos;
  newcurl->partdata.fd         = s3fscurl->partdata.fd;
  newcurl->partdata.startpos   = s3fscurl->b_partdata_startpos;
  newcurl->partdata.size       = s3fscurl->b_partdata_size;
  newcurl->b_partdata_startpos = s3fscurl->b_partdata_startpos;
  newcurl->b_partdata_size     = s3fscurl->b_partdata_size;
  newcurl->retry_count         = s3fscurl->retry_count + 1;

  // setup new curl object
  if(0 != newcurl->UploadMultipartPostSetup(s3fscurl->path.c_str(), part_num, upload_id)){
    S3FS_PRN_ERR("Could not duplicate curl object(%s:%d).", s3fscurl->path.c_str(), part_num);
    delete newcurl;
    return NULL;
  }
  return newcurl;
}

int S3fsCurl::ParallelMultipartUploadRequest(const char* tpath, headers_t& meta, int fd)
{
  int            result;
  string         upload_id;
  struct stat    st;
  int            fd2;
  etaglist_t     list;
  off_t          remaining_bytes;
  S3fsCurl       s3fscurl(true);

  S3FS_PRN_INFO3("[tpath=%s][fd=%d]", SAFESTRPTR(tpath), fd);

  // duplicate fd
  if(-1 == (fd2 = dup(fd)) || 0 != lseek(fd2, 0, SEEK_SET)){
    S3FS_PRN_ERR("Could not duplicate file descriptor(errno=%d)", errno);
    if(-1 != fd2){
      close(fd2);
    }
    return -errno;
  }
  if(-1 == fstat(fd2, &st)){
    S3FS_PRN_ERR("Invalid file descriptor(errno=%d)", errno);
    close(fd2);
    return -errno;
  }

  if(0 != (result = s3fscurl.PreMultipartPostRequest(tpath, meta, upload_id, false))){
    close(fd2);
    return result;
  }
  s3fscurl.DestroyCurlHandle();

  // cycle through open fd, pulling off 10MB chunks at a time
  for(remaining_bytes = st.st_size; 0 < remaining_bytes; ){
    S3fsMultiCurl curlmulti;
    int           para_cnt;
    off_t         chunk;

    // Initialize S3fsMultiCurl
    curlmulti.SetSuccessCallback(S3fsCurl::UploadMultipartPostCallback);
    curlmulti.SetRetryCallback(S3fsCurl::UploadMultipartPostRetryCallback);

    // Loop for setup parallel upload(multipart) request.
    for(para_cnt = 0; para_cnt < S3fsCurl::max_parallel_cnt && 0 < remaining_bytes; para_cnt++, remaining_bytes -= chunk){
      // chunk size
      chunk = remaining_bytes > S3fsCurl::multipart_size ? S3fsCurl::multipart_size : remaining_bytes;

      // s3fscurl sub object
      S3fsCurl* s3fscurl_para            = new S3fsCurl(true);
      s3fscurl_para->partdata.fd         = fd2;
      s3fscurl_para->partdata.startpos   = st.st_size - remaining_bytes;
      s3fscurl_para->partdata.size       = chunk;
      s3fscurl_para->b_partdata_startpos = s3fscurl_para->partdata.startpos;
      s3fscurl_para->b_partdata_size     = s3fscurl_para->partdata.size;
      s3fscurl_para->partdata.add_etag_list(&list);

      // initiate upload part for parallel
      if(0 != (result = s3fscurl_para->UploadMultipartPostSetup(tpath, list.size(), upload_id))){
        S3FS_PRN_ERR("failed uploading part setup(%d)", result);
        close(fd2);
        delete s3fscurl_para;
        return result;
      }

      // set into parallel object
      if(!curlmulti.SetS3fsCurlObject(s3fscurl_para)){
        S3FS_PRN_ERR("Could not make curl object into multi curl(%s).", tpath);
        close(fd2);
        delete s3fscurl_para;
        return -1;
      }
    }

    // Multi request
    if(0 != (result = curlmulti.Request())){
      S3FS_PRN_ERR("error occuered in multi request(errno=%d).", result);
      break;
    }

    // reinit for loop.
    curlmulti.Clear();
  }
  close(fd2);

  if(0 != (result = s3fscurl.CompleteMultipartPostRequest(tpath, upload_id, list))){
    return result;
  }
  return 0;
}

S3fsCurl* S3fsCurl::ParallelGetObjectRetryCallback(S3fsCurl* s3fscurl)
{
  int result;

  if(!s3fscurl){
    return NULL;
  }
  if(s3fscurl->retry_count >= S3fsCurl::retries){
    S3FS_PRN_ERR("Over retry count(%d) limit(%s).", s3fscurl->retry_count, s3fscurl->path.c_str());
    return NULL;
  }

  // duplicate request(setup new curl object)
  S3fsCurl* newcurl = new S3fsCurl(s3fscurl->IsUseAhbe());
  std::string path = s3fscurl->path;
  if (path.size() >= mount_prefix.size() && path.substr(0, mount_prefix.size()) == mount_prefix) {
    path = path.substr(mount_prefix.size());
  }
  if(0 != (result = newcurl->PreGetObjectRequest(path.c_str(), s3fscurl->partdata.fd,
     s3fscurl->partdata.startpos, s3fscurl->partdata.size, s3fscurl->b_ssetype, s3fscurl->b_ssevalue)))
  {
    S3FS_PRN_ERR("failed downloading part setup(%d)", result);
    delete newcurl;
    return NULL;;
  }
  newcurl->retry_count = s3fscurl->retry_count + 1;

  return newcurl;
}

int S3fsCurl::ParallelGetObjectRequest(const char* tpath, int fd, off_t start, ssize_t size)
{
  S3FS_PRN_INFO3("[tpath=%s][fd=%d]", SAFESTRPTR(tpath), fd);

  sse_type_t ssetype;
  string     ssevalue;

  int        result = 0;
  ssize_t    remaining_bytes;

  // cycle through open fd, pulling off 10MB chunks at a time
  for(remaining_bytes = size; 0 < remaining_bytes; ){
    S3fsMultiCurl curlmulti;
    int           para_cnt;
    off_t         chunk;

    // Initialize S3fsMultiCurl
    //curlmulti.SetSuccessCallback(NULL);   // not need to set success callback
    curlmulti.SetRetryCallback(S3fsCurl::ParallelGetObjectRetryCallback);

    // Loop for setup parallel upload(multipart) request.
    for(para_cnt = 0; para_cnt < S3fsCurl::max_parallel_cnt && 0 < remaining_bytes; para_cnt++, remaining_bytes -= chunk){
      // chunk size
      chunk = remaining_bytes > S3fsCurl::multipart_size ? S3fsCurl::multipart_size : remaining_bytes;

      // s3fscurl sub object
      S3fsCurl* s3fscurl_para = new S3fsCurl();
      if(0 != (result = s3fscurl_para->PreGetObjectRequest(tpath, fd, (start + size - remaining_bytes), chunk, ssetype, ssevalue))){
        S3FS_PRN_ERR("failed downloading part setup(%d)", result);
        delete s3fscurl_para;
        return result;
      }

      // set into parallel object
      if(!curlmulti.SetS3fsCurlObject(s3fscurl_para)){
        S3FS_PRN_ERR("Could not make curl object into multi curl(%s).", tpath);
        delete s3fscurl_para;
        return -1;
      }
    }

    // Multi request
    if(0 != (result = curlmulti.Request())){
      S3FS_PRN_ERR("error occuered in multi request(errno=%d).", result);
      break;
    }

    // reinit for loop.
    curlmulti.Clear();
  }
  return result;
}

bool S3fsCurl::ParseRAMCredentialResponse(const char* response, ramcredmap_t& keyval)
{
  if(!response){
    return false;
  }
  istringstream sscred(response);
  string        oneline;
  keyval.clear();
  while(getline(sscred, oneline, '\n')){
    string::size_type pos;
    string            key;
    string            val;
    if(string::npos != (pos = oneline.find(RAMCRED_ACCESSKEYID))){
      key = RAMCRED_ACCESSKEYID;
    }else if(string::npos != (pos = oneline.find(RAMCRED_SECRETACCESSKEY))){
      key = RAMCRED_SECRETACCESSKEY;
    }else if(string::npos != (pos = oneline.find(RAMCRED_ACCESSTOKEN))){
      key = RAMCRED_ACCESSTOKEN;
    }else if(string::npos != (pos = oneline.find(RAMCRED_EXPIRATION))){
      key = RAMCRED_EXPIRATION;
    }else{
      continue;
    }
    if(string::npos == (pos = oneline.find(':', pos + key.length()))){
      continue;
    }
    if(string::npos == (pos = oneline.find('\"', pos))){
      continue;
    }
    oneline = oneline.substr(pos + sizeof(char));
    if(string::npos == (pos = oneline.find('\"'))){
      continue;
    }
    val = oneline.substr(0, pos);
    keyval[key] = val;
  }
  return true;
}

bool S3fsCurl::SetRAMCredentials(const char* response)
{
  S3FS_PRN_INFO3("RAM credential response = \"%s\"", response);

  ramcredmap_t keyval;

  if(!ParseRAMCredentialResponse(response, keyval)){
    S3FS_PRN_ERR("could not parse RAM credential response.");
    return false;
  }
  if(RAMCRED_KEYCOUNT != keyval.size()){
    S3FS_PRN_ERR("parse RAM credential response failed, key count: %ld.", keyval.size());
    return false;
  }

  AutoLock auto_lock(&token_lock);
  S3fsCurl::COSAccessKeyId       = keyval[string(RAMCRED_ACCESSKEYID)];
  S3fsCurl::COSSecretAccessKey   = keyval[string(RAMCRED_SECRETACCESSKEY)];
  S3fsCurl::COSAccessToken       = keyval[string(RAMCRED_ACCESSTOKEN)];
  S3fsCurl::COSAccessTokenExpire = cvtCAMExpireStringToTime(keyval[string(RAMCRED_EXPIRATION)].c_str());
  return true;
}

bool S3fsCurl::SetUserAgentSuffix(const std::string& suffix) {
  skUserAgent = "tencentyun-cosfs-v5-";
  skUserAgent +=  suffix + "-" + VERSION;
  return true;
}

bool S3fsCurl::CheckRAMCredentialUpdate(void)
{
  if(0 == S3fsCurl::CAM_role.size()){
    return true;
  }
  {
	AutoLock auto_lock(&token_lock);
	if(time(NULL) + RAM_EXPIRE_MERGIN <= S3fsCurl::COSAccessTokenExpire){
		return true;
	}
  }
  // update
  S3fsCurl s3fscurl;
  if(0 != s3fscurl.GetRAMCredentials()){
    return false;
  }
  return true;
}

int S3fsCurl::CurlDebugFunc(CURL* hcurl, curl_infotype type, char* data, size_t size, void* userptr)
{
  if(!hcurl){
    // something wrong...
    return 0;
  }
  switch(type){
    case CURLINFO_TEXT:
    case CURLINFO_HEADER_IN:
    case CURLINFO_HEADER_OUT:
      char* buff;
      if(NULL == (buff = reinterpret_cast<char*>(malloc(size + 2 + 1)))){
        // could not allocation memory
        S3FS_PRN_CRIT("could not allocate memory");
        break;
      }
      buff[size + 2] = '\0';
      sprintf(buff, "%c ", (CURLINFO_TEXT == type ? '*' : CURLINFO_HEADER_IN == type ? '<' : '>'));
      memcpy(&buff[2], data, size);
      S3FS_PRN_CURL("%s", buff);      // no blocking
      free(buff);
      break;
    case CURLINFO_DATA_IN:
    case CURLINFO_DATA_OUT:
    case CURLINFO_SSL_DATA_IN:
    case CURLINFO_SSL_DATA_OUT:
      // not put
      break;
    default:
      // why
      break;
  }
  return 0;
}

//-------------------------------------------------------------------
// Methods for S3fsCurl
//-------------------------------------------------------------------
S3fsCurl::S3fsCurl(bool ahbe) :
    hCurl(NULL), path(""), base_path(""), saved_path(""), url(""), requestHeaders(NULL),
    bodydata(NULL), headdata(NULL), LastResponseCode(-1), postdata(NULL), postdata_remaining(0), is_use_ahbe(ahbe),
    retry_count(0), b_infile(NULL), b_postdata(NULL), b_postdata_remaining(0), b_partdata_startpos(0), b_partdata_size(0),
    b_ssekey_pos(-1), b_ssevalue(""), b_ssetype(SSE_DISABLE)
{
  type = REQTYPE_UNSET;
}

S3fsCurl::~S3fsCurl()
{
  DestroyCurlHandle();
}

bool S3fsCurl::ResetHandle(void)
{
  curl_easy_reset(hCurl);
  curl_easy_setopt(hCurl, CURLOPT_NOSIGNAL, 1);
  curl_easy_setopt(hCurl, CURLOPT_FOLLOWLOCATION, true);
  curl_easy_setopt(hCurl, CURLOPT_CONNECTTIMEOUT, S3fsCurl::connect_timeout);
  curl_easy_setopt(hCurl, CURLOPT_NOPROGRESS, 0);
  curl_easy_setopt(hCurl, CURLOPT_PROGRESSFUNCTION, S3fsCurl::CurlProgress);
  curl_easy_setopt(hCurl, CURLOPT_PROGRESSDATA, hCurl);
  // curl_easy_setopt(hCurl, CURLOPT_FORBID_REUSE, 1);

  if((S3fsCurl::is_dns_cache || S3fsCurl::is_ssl_session_cache) && S3fsCurl::hCurlShare){
    curl_easy_setopt(hCurl, CURLOPT_SHARE, S3fsCurl::hCurlShare);
  }
  if(!S3fsCurl::is_cert_check) {
    S3FS_PRN_DBG("'no_check_certificate' option in effect.")
    S3FS_PRN_DBG("The server certificate won't be checked against the available certificate authorities.")
    curl_easy_setopt(hCurl, CURLOPT_SSL_VERIFYPEER, false);
  }
  if(S3fsCurl::is_verbose){
    curl_easy_setopt(hCurl, CURLOPT_VERBOSE, true);
    if(!foreground){
      curl_easy_setopt(hCurl, CURLOPT_DEBUGFUNCTION, S3fsCurl::CurlDebugFunc);
    }
  }

  S3fsCurl::curl_times[hCurl]    = time(0);
  S3fsCurl::curl_progress[hCurl] = progress_t(-1, -1);

  return true;
}

bool S3fsCurl::CreateCurlHandle(bool force)
{
  pthread_mutex_lock(&S3fsCurl::curl_handles_lock);

  if(hCurl){
    if(!force){
      S3FS_PRN_WARN("already create handle.");
      return false;
    }
    if(!DestroyCurlHandle()){
      S3FS_PRN_ERR("could not destroy handle.");
      return false;
    }
    S3FS_PRN_INFO3("already has handle, so destroied it.");
  }

  if(NULL == (hCurl = sCurlPool->GetHandler())){
    S3FS_PRN_ERR("Failed to create handle.");
    return false;
  }
  type = REQTYPE_UNSET;
  ResetHandle();

  pthread_mutex_unlock(&S3fsCurl::curl_handles_lock);

  return true;
}

bool S3fsCurl::DestroyCurlHandle(void)
{
  if(!hCurl){
    return false;
  }
  pthread_mutex_lock(&S3fsCurl::curl_handles_lock);

  S3fsCurl::curl_times.erase(hCurl);
  S3fsCurl::curl_progress.erase(hCurl);
  sCurlPool->ReturnHandler(hCurl);
  hCurl = NULL;
  ClearInternalData();

  pthread_mutex_unlock(&S3fsCurl::curl_handles_lock);
  return true;
}

bool S3fsCurl::ClearInternalData(void)
{
  if(hCurl){
    return false;
  }
  type      = REQTYPE_UNSET;
  path      = "";
  base_path = "";
  saved_path= "";
  url       = "";
  if(requestHeaders){
    curl_slist_free_all(requestHeaders);
    requestHeaders = NULL;
  }
  responseHeaders.clear();
  if(bodydata){
    delete bodydata;
    bodydata = NULL;
  }
  if(headdata){
    delete headdata;
    headdata = NULL;
  }
  LastResponseCode     = -1;
  postdata             = NULL;
  postdata_remaining   = 0;
  retry_count          = 0;
  b_infile             = NULL;
  b_postdata           = NULL;
  b_postdata_remaining = 0;
  b_partdata_startpos  = 0;
  b_partdata_size      = 0;
  partdata.clear();

  S3FS_MALLOCTRIM(0);

  return true;
}

bool S3fsCurl::SetUseAhbe(bool ahbe)
{
  bool old = is_use_ahbe;
  is_use_ahbe = ahbe;
  return old;
}

bool S3fsCurl::GetResponseCode(long& responseCode)
{
  if(!hCurl){
    return false;
  }
  responseCode = -1;
  if(CURLE_OK != curl_easy_getinfo(hCurl, CURLINFO_RESPONSE_CODE, &LastResponseCode)){
    return false;
  }
  responseCode = LastResponseCode;
  return true;
}

//
// Reset all options for retrying
//
bool S3fsCurl::RemakeHandle(void)
{
  S3FS_PRN_INFO3("Retry request. [type=%d][url=%s][path=%s]", type, url.c_str(), path.c_str());

  if(REQTYPE_UNSET == type){
    return false;
  }

  // rewind file
  struct stat st;
  if(b_infile){
    rewind(b_infile);
    if(-1 == fstat(fileno(b_infile), &st)){
      S3FS_PRN_WARN("Could not get file stat(fd=%d)", fileno(b_infile));
      return false;
    }
  }

  // reinitialize internal data
  responseHeaders.clear();
  if(bodydata){
    bodydata->Clear();
  }
  if(headdata){
    headdata->Clear();
  }
  LastResponseCode   = -1;

  // count up(only use for multipart)
  retry_count++;

  // set from backup
  postdata           = b_postdata;
  postdata_remaining = b_postdata_remaining;
  partdata.startpos  = b_partdata_startpos;
  partdata.size      = b_partdata_size;

  // reset handle
  ResetHandle();

  // set options
  switch(type){
    case REQTYPE_DELETE:
      curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(hCurl, CURLOPT_CUSTOMREQUEST, "DELETE");
      curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);
      break;

    case REQTYPE_HEAD:
      curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(hCurl, CURLOPT_NOBODY, true);
      curl_easy_setopt(hCurl, CURLOPT_FILETIME, true);
      curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);
      // responseHeaders
      curl_easy_setopt(hCurl, CURLOPT_HEADERDATA, (void*)&responseHeaders);
      curl_easy_setopt(hCurl, CURLOPT_HEADERFUNCTION, HeaderCallback);
      break;

    case REQTYPE_PUTHEAD:
      curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(hCurl, CURLOPT_UPLOAD, true);
      curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)bodydata);
      curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
      curl_easy_setopt(hCurl, CURLOPT_INFILESIZE, 0);
      curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);
      break;

    case REQTYPE_PUT:
      curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(hCurl, CURLOPT_UPLOAD, true);
      curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)bodydata);
      curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
      curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);
      if(b_infile){
        curl_easy_setopt(hCurl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(st.st_size));
        curl_easy_setopt(hCurl, CURLOPT_INFILE, b_infile);
      }else{
        curl_easy_setopt(hCurl, CURLOPT_INFILESIZE, 0);
      }
      break;

    case REQTYPE_GET:
      curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);
      curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, S3fsCurl::DownloadWriteCallback);
      curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)this);
      break;

    case REQTYPE_CHKBUCKET:
      curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
	  // XXX
      //curl_easy_setopt(hCurl, CURLOPT_FAILONERROR, true);
      curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)bodydata);
      curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
      curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);
      break;

    case REQTYPE_LISTBUCKET:
      curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)bodydata);
      curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
      curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);
      break;

    case REQTYPE_PREMULTIPOST:
      curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(hCurl, CURLOPT_POST, true);
      curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)bodydata);
      curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
      curl_easy_setopt(hCurl, CURLOPT_POSTFIELDSIZE, 0);
      curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);
      break;

    case REQTYPE_COMPLETEMULTIPOST:
      curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);
      curl_easy_setopt(hCurl, CURLOPT_POST, true);
      curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)bodydata);
      curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
      curl_easy_setopt(hCurl, CURLOPT_POSTFIELDSIZE, static_cast<curl_off_t>(postdata_remaining));
      curl_easy_setopt(hCurl, CURLOPT_READDATA, (void*)this);
      curl_easy_setopt(hCurl, CURLOPT_READFUNCTION, S3fsCurl::ReadCallback);
      break;

    case REQTYPE_UPLOADMULTIPOST:
      curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(hCurl, CURLOPT_UPLOAD, true);
      curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)bodydata);
      curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
      curl_easy_setopt(hCurl, CURLOPT_HEADERDATA, (void*)headdata);
      curl_easy_setopt(hCurl, CURLOPT_HEADERFUNCTION, WriteMemoryCallback);
      curl_easy_setopt(hCurl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(partdata.size));
      curl_easy_setopt(hCurl, CURLOPT_READFUNCTION, S3fsCurl::UploadReadCallback);
      curl_easy_setopt(hCurl, CURLOPT_READDATA, (void*)this);
      curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);
      break;

    case REQTYPE_COPYMULTIPOST:
      curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(hCurl, CURLOPT_UPLOAD, true);
      curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)bodydata);
      curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
      curl_easy_setopt(hCurl, CURLOPT_HEADERDATA, (void*)headdata);
      curl_easy_setopt(hCurl, CURLOPT_HEADERFUNCTION, WriteMemoryCallback);
      curl_easy_setopt(hCurl, CURLOPT_INFILESIZE, 0);
      curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);
      break;

    case REQTYPE_MULTILIST:
      curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)bodydata);
      curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
      curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);
      break;

    case REQTYPE_RAMCRED:
      curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)bodydata);
      curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
      break;

    case REQTYPE_ABORTMULTIUPLOAD:
      curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(hCurl, CURLOPT_CUSTOMREQUEST, "DELETE");
      curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);
      break;

    default:
      S3FS_PRN_ERR("request type is unknown(%d)", type);
      return false;
  }
  return true;
}

//
// returns curl return code
//
int S3fsCurl::RequestPerform(void)
{
  static const int max_retry_time_in_s = 10;
  // Add the user-agent info
  requestHeaders = curl_slist_sort_insert(requestHeaders, "User-Agent", skUserAgent.c_str());
  if(IS_S3FS_LOG_DBG()){
    char* ptr_url = NULL;
    curl_easy_getinfo(hCurl, CURLINFO_EFFECTIVE_URL , &ptr_url);
    S3FS_PRN_DBG("connecting to URL %s", SAFESTRPTR(ptr_url));
  }

#ifdef TEST_COSFS
  test_request_count = 0;
#endif
  // 1 attempt + retries...
  for(int retrycnt = S3fsCurl::retries; 0 < retrycnt; retrycnt--){

#ifdef TEST_COSFS
	test_request_count++;
#endif
    // Requests
	// XXX
	//curl_easy_setopt(hCurl, CURLOPT_HEADERDATA, (void*)&responseHeaders);
	//curl_easy_setopt(hCurl, CURLOPT_HEADERFUNCTION, HeaderCallback);
    CURLcode curlCode = curl_easy_perform(hCurl);

	// exponential backoff until max_retry_time_in_s
	int retry_sleep_time_in_s = 2 * (retries - retrycnt + 1);
	if (retry_sleep_time_in_s > max_retry_time_in_s) {
		retry_sleep_time_in_s = max_retry_time_in_s;
	}

    // Check result
    switch(curlCode){
      case CURLE_OK:
        // Need to look at the HTTP response code
        if(0 != curl_easy_getinfo(hCurl, CURLINFO_RESPONSE_CODE, &LastResponseCode)){
          S3FS_PRN_ERR("curl_easy_getinfo failed while trying to retrieve HTTP response code");
          return -EIO;
        }
        if(400 > LastResponseCode){
          S3FS_PRN_INFO3("HTTP response code %ld", LastResponseCode);
          return 0;
        }
        if(500 <= LastResponseCode){
          S3FS_PRN_INFO3("HTTP response code %ld", LastResponseCode);
          sleep(retry_sleep_time_in_s);
          break;
        }

        // Service response codes which are >= 400 && < 500
        switch(LastResponseCode){
          case 400:
            S3FS_PRN_INFO3("HTTP response code 400 was returned, returing EIO.");
            S3FS_PRN_DBG("Body Text: %s", (bodydata ? bodydata->str() : ""));
            return -EIO;

          case 403:
            S3FS_PRN_INFO3("HTTP response code 403 was returned, returning EPERM");
            S3FS_PRN_DBG("Body Text: %s", (bodydata ? bodydata->str() : ""));
            return -EPERM;

          case 404:
            S3FS_PRN_INFO3("HTTP response code 404 was returned, returning ENOENT");
            S3FS_PRN_DBG("Body Text: %s", (bodydata ? bodydata->str() : ""));
            return -ENOENT;

          case 409:
            S3FS_PRN_INFO3("HTTP response code 409 was returned, retry after sleep");
            S3FS_PRN_DBG("Body Text: %s", (bodydata ? bodydata->str() : ""));
            usleep(100000);
            break;

          default:
            S3FS_PRN_INFO3("HTTP response code = %ld, returning EIO", LastResponseCode);
            S3FS_PRN_DBG("Body Text: %s", (bodydata ? bodydata->str() : ""));
            return -EIO;
        }
        break;

      case CURLE_WRITE_ERROR:
        S3FS_PRN_ERR("### CURLE_WRITE_ERROR");
        sleep(retry_sleep_time_in_s);
        break;

      case CURLE_OPERATION_TIMEDOUT:
        S3FS_PRN_ERR("### CURLE_OPERATION_TIMEDOUT");
        sleep(retry_sleep_time_in_s);
        break;

      case CURLE_COULDNT_RESOLVE_HOST:
        S3FS_PRN_ERR("### CURLE_COULDNT_RESOLVE_HOST");
        sleep(retry_sleep_time_in_s);
        break;

      case CURLE_COULDNT_CONNECT:
        S3FS_PRN_ERR("### CURLE_COULDNT_CONNECT");
        sleep(retry_sleep_time_in_s);
        break;

      case CURLE_GOT_NOTHING:
        S3FS_PRN_ERR("### CURLE_GOT_NOTHING");
        sleep(retry_sleep_time_in_s);
        break;

      case CURLE_ABORTED_BY_CALLBACK:
        S3FS_PRN_ERR("### CURLE_ABORTED_BY_CALLBACK");
        // sleep(retry_sleep_time_in_s);
        S3fsCurl::curl_times[hCurl] = time(0);
        break;

      case CURLE_PARTIAL_FILE:
        S3FS_PRN_ERR("### CURLE_PARTIAL_FILE");
        sleep(retry_sleep_time_in_s);
        break;

      case CURLE_SEND_ERROR:
        S3FS_PRN_ERR("### CURLE_SEND_ERROR");
        sleep(retry_sleep_time_in_s);
        break;

      case CURLE_RECV_ERROR:
        S3FS_PRN_ERR("### CURLE_RECV_ERROR");
        sleep(retry_sleep_time_in_s);
        break;

      case CURLE_SSL_CONNECT_ERROR:
        S3FS_PRN_ERR("### CURLE_SSL_CONNECT_ERROR");
        sleep(retry_sleep_time_in_s);
        break;

      case CURLE_SSL_CACERT:
        S3FS_PRN_ERR("### CURLE_SSL_CACERT");

        // try to locate cert, if successful, then set the
        // option and continue
        if(0 == S3fsCurl::curl_ca_bundle.size()){
          if(!S3fsCurl::LocateBundle()){
            S3FS_PRN_CRIT("could not get CURL_CA_BUNDLE.");
            return -EIO;
          }
          break; // retry with CAINFO
        }
        S3FS_PRN_CRIT("curlCode: %d  msg: %s", curlCode, curl_easy_strerror(curlCode));
        return -EIO;

#ifdef CURLE_PEER_FAILED_VERIFICATION
      case CURLE_PEER_FAILED_VERIFICATION:
        S3FS_PRN_ERR("### CURLE_PEER_FAILED_VERIFICATION");

        first_pos = bucket.find_first_of(".");
        if(first_pos != string::npos){
          S3FS_PRN_INFO("curl returned a CURL_PEER_FAILED_VERIFICATION error");
          S3FS_PRN_INFO("security issue found: buckets with periods in their name are incompatible with http");
          S3FS_PRN_INFO("This check can be over-ridden by using the -o ssl_verify_hostname=0");
          S3FS_PRN_INFO("The certificate will still be checked but the hostname will not be verified.");
          S3FS_PRN_INFO("A more secure method would be to use a bucket name without periods.");
        }else{
          S3FS_PRN_INFO("my_curl_easy_perform: curlCode: %d -- %s", curlCode, curl_easy_strerror(curlCode));
        }
        return -EIO;
#endif

      // This should be invalid since curl option HTTP FAILONERROR is now off
      case CURLE_HTTP_RETURNED_ERROR:
        S3FS_PRN_ERR("### CURLE_HTTP_RETURNED_ERROR");

        if(0 != curl_easy_getinfo(hCurl, CURLINFO_RESPONSE_CODE, &LastResponseCode)){
          return -EIO;
        }
        S3FS_PRN_INFO3("HTTP response code =%ld", LastResponseCode);

        // Let's try to retrieve the
        if(404 == LastResponseCode){
          return -ENOENT;
        }
        if(500 > LastResponseCode){
          return -EIO;
        }
        break;

      // Unknown CURL return code
      default:
        S3FS_PRN_CRIT("###curlCode: %d  msg: %s", curlCode, curl_easy_strerror(curlCode));
        return -EIO;
    }
    S3FS_PRN_INFO("### retrying...");

    if(!RemakeHandle()){
      S3FS_PRN_INFO("Failed to reset handle and internal data for retrying.");
      return -EIO;
    }
  }
  S3FS_PRN_ERR("### giving up");

  return -EIO;
}

//
// Returns the Tencent COS signature for the given parameters.
//
// @param method e.g., "GET"
// @param content_type e.g., "application/x-directory"
// @param date e.g., get_date_rfc850()
// @param resource e.g., "/pub"
//
string S3fsCurl::CalcSignature(string method, string strMD5, string content_type, string date, string resource, string query)
{
  string Signature;
  string accessKey, secretKey, accessToken;

  if (0 < S3fsCurl::CAM_role.size()) {
    if (0 < S3fsCurl::CAM_role_url.size()) {
      if (!S3fsCurl::CheckRAMCredentialUpdate()) {
        S3FS_PRN_ERR("Something error occurred in checking CAM Role Credential");
        return Signature;
      }
    } else {
      if (!S3fsCurl::checkSTSCredentialUpdate()) {
        S3FS_PRN_ERR("Something error occurred in checking CAM STS Credential");
        return Signature;
      }
    }
    S3fsCurl::GetAccessKeyWithToken(accessKey, secretKey, accessToken);
    requestHeaders = curl_slist_sort_insert(requestHeaders, "x-cos-security-token", accessToken.c_str());
  } else {
    S3fsCurl::GetAccessKey(accessKey, secretKey);
  }

  const void* key            = secretKey.data();
  int key_len                = secretKey.size();

  // first, get sign key
  time_t key_t_s = time(NULL) - 10;
  time_t key_t_e = key_t_s + 3700; // expired after 1 hour
  ostringstream q_key_time_tmp;
  q_key_time_tmp << key_t_s << ";" << key_t_e;
  string q_key_time = q_key_time_tmp.str();
  const unsigned char* kdata = reinterpret_cast<const unsigned char*>(q_key_time.data());

  unsigned char* sign_key_raw = NULL;
  unsigned int sign_key_len = 0;
  s3fs_HMAC(key, key_len, kdata, q_key_time.size(), &sign_key_raw, &sign_key_len);
  std::string sign_key = s3fs_hex(sign_key_raw, sign_key_len);

  map<string, string> requestParams = get_params_from_query_string(query);
  string FormatString;
  FormatString += lower(method) + "\n";
  FormatString += resource + "\n";
  FormatString += get_canonical_params(requestParams); // \n has been append
  FormatString += get_canonical_headers(requestHeaders); // \n has been append

  const unsigned char* sdata = reinterpret_cast<const unsigned char*>(FormatString.data());
  int sdata_len              = FormatString.size();
  unsigned char* md          = NULL;
  unsigned int md_len        = 0;

  string format_string_sha1 = s3fs_sha1_hex(sdata, sdata_len, &md, &md_len);
  string StringToSign;
  StringToSign += string("sha1\n");
  StringToSign += q_key_time + "\n";
  StringToSign += format_string_sha1 + "\n";

  unsigned char* sign_data     = NULL;
  unsigned int sign_len        = 0;
  s3fs_HMAC(sign_key.data(), sign_key.size(), reinterpret_cast<const unsigned char*>(StringToSign.data()), StringToSign.size(), &sign_data, &sign_len);
  string sign_data_hex = s3fs_hex(sign_data, sign_len);

  Signature += "q-sign-algorithm=sha1&";
  Signature += string("q-ak=") + accessKey + "&";
  Signature += string("q-sign-time=") + q_key_time + "&";
  Signature += string("q-key-time=") + q_key_time + "&q-url-param-list=" + get_canonical_param_keys(requestParams) + "&";
  Signature += string("q-header-list=") + get_canonical_header_keys(requestHeaders) + "&";
  Signature += string("q-signature=") + sign_data_hex;

  free(sign_key_raw);
  free(md);
  free(sign_data);
  return Signature;
}

// XML in BodyData has UploadId, Parse XML body for UploadId
bool S3fsCurl::GetUploadId(string& upload_id)
{
  bool result = false;

  if(!bodydata){
    return result;
  }
  upload_id.clear();

  xmlDocPtr doc;
  if(NULL == (doc = xmlReadMemory(bodydata->str(), bodydata->size(), "", NULL, 0))){
    return result;
  }
  if(NULL == doc->children){
    S3FS_XMLFREEDOC(doc);
    return result;
  }
  for(xmlNodePtr cur_node = doc->children->children; NULL != cur_node; cur_node = cur_node->next){
    // For DEBUG
    // string cur_node_name(reinterpret_cast<const char *>(cur_node->name));
    // printf("cur_node_name: %s\n", cur_node_name.c_str());

    if(XML_ELEMENT_NODE == cur_node->type){
      string elementName = reinterpret_cast<const char*>(cur_node->name);
      // For DEBUG
      // printf("elementName: %s\n", elementName.c_str());

      if(cur_node->children){
        if(XML_TEXT_NODE == cur_node->children->type){
          if(elementName == "UploadId") {
            upload_id = reinterpret_cast<const char *>(cur_node->children->content);
            result    = true;
            break;
          }
        }
      }
    }
  }
  S3FS_XMLFREEDOC(doc);

  return result;
}

int S3fsCurl::DeleteRequest(const char* tpath, int pid)
{
  S3FS_PRN_INFO3("[tpath=%s]", SAFESTRPTR(tpath));

  if(!tpath){
    return -1;
  }
  if(!CreateCurlHandle(true)){
    return -1;
  }
  string resource;
  string turl;
  string host;
  MakeUrlResource(get_realpath(tpath).c_str(), resource, turl);

  url             = prepare_url(turl.c_str(), host);
  path            = get_realpath(tpath);
  requestHeaders  = NULL;
  responseHeaders.clear();

  string date    = get_date_rfc850();
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Host", host.c_str());
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Date", date.c_str());
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Content-Type", NULL);
  if(!S3fsCurl::IsPublicBucket()){
	  string Signature = CalcSignature("DELETE", "", "", date, resource, "");
	  requestHeaders   = curl_slist_sort_insert(requestHeaders, "Authorization", Signature.c_str());
  }
  if(S3fsCurl::IsClientInfoInDelete()){
      ostringstream pidstr;
      pidstr << pid;
      string clientinfo = S3fsCurl::GetClientInfo(pidstr.str());
      requestHeaders = curl_slist_sort_insert(requestHeaders, "x-delete-client-pid", pidstr.str().c_str());
      requestHeaders = curl_slist_sort_insert(requestHeaders, "x-delete-client-cgroup", clientinfo.c_str());
  }

  curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(hCurl, CURLOPT_CUSTOMREQUEST, "DELETE");
  curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);

  type = REQTYPE_DELETE;

  return RequestPerform();
}

//
// Get AccessKeyId/SecretAccessKey/AccessToken/Expiration by RAM role,
// and Set these value to class valiable.
//
int S3fsCurl::GetRAMCredentials(void)
{
  S3FS_PRN_INFO3("[RAM role=%s]", S3fsCurl::CAM_role.c_str());

  if(0 == S3fsCurl::CAM_role.size()){
    S3FS_PRN_ERR("RAM role name is empty.");
    return -EIO;
  }
  // at first set type for handle
  type = REQTYPE_RAMCRED;

  if(!CreateCurlHandle(true)){
    return -EIO;
  }

  // url
  url             = S3fsCurl::CAM_role_url + "/" + S3fsCurl::CAM_role;
  requestHeaders  = NULL;
  responseHeaders.clear();
  bodydata        = new BodyData();

  curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)bodydata);
  curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

  int result = RequestPerform();

  // analizing response
  if(0 == result && !S3fsCurl::SetRAMCredentials(bodydata->str())){
    S3FS_PRN_ERR("Something error occurred, could not get RAM credential.");
  }
  delete bodydata;
  bodydata = NULL;

  return result;
}

bool S3fsCurl::AddSseRequestHead(sse_type_t ssetype, string& ssevalue, bool is_only_c, bool is_copy)
{
  if(SSE_OSS == ssetype){
    if(!is_only_c){
      requestHeaders = curl_slist_sort_insert(requestHeaders, "x-cos-server-side-encryption", "AES256");
    }
  }else if(SSE_C == ssetype){
    string sseckey;
    if(S3fsCurl::GetSseKey(ssevalue, sseckey)){
      if(is_copy){
        requestHeaders = curl_slist_sort_insert(requestHeaders, "x-cos-copy-source-server-side-encryption-customer-algorithm", "AES256");
        requestHeaders = curl_slist_sort_insert(requestHeaders, "x-cos-copy-source-server-side-encryption-customer-key",       sseckey.c_str());
        requestHeaders = curl_slist_sort_insert(requestHeaders, "x-cos-copy-source-server-side-encryption-customer-key-md5",   ssevalue.c_str());
      }else{
        requestHeaders = curl_slist_sort_insert(requestHeaders, "x-cos-server-side-encryption-customer-algorithm", "AES256");
        requestHeaders = curl_slist_sort_insert(requestHeaders, "x-cos-server-side-encryption-customer-key",       sseckey.c_str());
        requestHeaders = curl_slist_sort_insert(requestHeaders, "x-cos-server-side-encryption-customer-key-md5",   ssevalue.c_str());
      }
    }else{
      S3FS_PRN_WARN("Failed to insert SSE-C header.");
    }

  }else if(SSE_KMS == ssetype){
	// Do not support KMS
	return false;
  }
  return true;
}

//
// tpath :      target path for head request
// bpath :      saved into base_path
// savedpath :  saved into saved_path
// ssekey_pos : -1    means "not" SSE-C type
//              0 - X means SSE-C type and position for SSE-C key(0 is latest key)
//
bool S3fsCurl::PreHeadRequest(const char* tpath, const char* bpath, const char* savedpath, int ssekey_pos)
{
  S3FS_PRN_INFO3("[tpath=%s][bpath=%s][save=%s][sseckeypos=%d]", SAFESTRPTR(tpath), SAFESTRPTR(bpath), SAFESTRPTR(savedpath), ssekey_pos);

  if(!tpath){
    return false;
  }
  if(!CreateCurlHandle(true)){
    return false;
  }
  string resource;
  string turl;
  string host;
  MakeUrlResource(get_realpath(tpath).c_str(), resource, turl);

  // libcurl 7.17 does deep copy of url, deep copy "stable" url
  url             = prepare_url(turl.c_str(), host);
  path            = get_realpath(tpath);
  base_path       = SAFESTRPTR(bpath);
  saved_path      = SAFESTRPTR(savedpath);
  requestHeaders  = NULL;
  responseHeaders.clear();

  // requestHeaders
  b_ssekey_pos = ssekey_pos;

  string date    = get_date_rfc850();
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Host", host.c_str());
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Date", date.c_str());
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Content-Type", NULL);
  requestHeaders = curl_slist_sort_insert(requestHeaders, "User-Agent", skUserAgent.c_str());

  if(!S3fsCurl::IsPublicBucket()){
	  string Signature = CalcSignature("HEAD", "", "", date, resource, "");
	  requestHeaders   = curl_slist_sort_insert(requestHeaders, "Authorization", Signature.c_str());
  }

  curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(hCurl, CURLOPT_NOBODY, true);   // HEAD
  curl_easy_setopt(hCurl, CURLOPT_FILETIME, true); // Last-Modified
  curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);

  // responseHeaders
  curl_easy_setopt(hCurl, CURLOPT_HEADERDATA, (void*)&responseHeaders);
  curl_easy_setopt(hCurl, CURLOPT_HEADERFUNCTION, HeaderCallback);

  type = REQTYPE_HEAD;

  return true;
}

int S3fsCurl::HeadRequest(const char* tpath, headers_t& meta)
{
  int result = -1;

  S3FS_PRN_INFO3("[tpath=%s]", SAFESTRPTR(tpath));

  // At first, try to get without SSE-C headers
  if(!PreHeadRequest(tpath) || 0 != (result = RequestPerform())){
    if(0 != result){
      DestroyCurlHandle();  // not check result.
      return result;
    }
  }

  // file exists in s3
  // fixme: clean this up.
  meta.clear();
  for(headers_t::iterator iter = responseHeaders.begin(); iter != responseHeaders.end(); ++iter){
    string key   = lower(iter->first);
    string value = iter->second;
    if(key == "content-type"){
      meta["Content-Type"] = value;
    }else if(key == "content-length"){
      meta["Content-Length"] = value;
    }else if(key == "etag"){
      meta["ETag"] = value;
    }else if(key == "last-modified"){
      meta["Last-Modified"] = value;
    }else if(key.substr(0, 5) == "x-cos"){
      meta[key] = value;		// key is lower case for "x-cos"
    }
  }
  return 0;
}

int S3fsCurl::PutHeadRequest(const char* tpath, headers_t& meta, bool is_copy)
{
  S3FS_PRN_INFO3("[tpath=%s]", SAFESTRPTR(tpath));

  if(!tpath){
    return -1;
  }
  if(!CreateCurlHandle(true)){
    return -1;
  }
  string resource;
  string turl;
  string host;
  MakeUrlResource(get_realpath(tpath).c_str(), resource, turl);

  url             = prepare_url(turl.c_str(), host);
  path            = get_realpath(tpath);
  requestHeaders  = NULL;
  responseHeaders.clear();
  bodydata        = new BodyData();

  // Make request headers
  for(headers_t::iterator iter = meta.begin(); iter != meta.end(); ++iter){
    string key   = lower(iter->first);
    string value = iter->second;
    if(key.substr(0, 9) == "x-cos-acl"){
      // not set value, but after set it.
    }else if(key.substr(0, 10) == "x-cos-meta"){
      requestHeaders = curl_slist_sort_insert(requestHeaders, iter->first.c_str(), value.c_str());
    } else if(key == "x-cos-copy-source"){
      requestHeaders = curl_slist_sort_insert(requestHeaders, iter->first.c_str(), value.c_str());
    }
  }

  // "x-cos-acl", storage class, sse
  // requestHeaders = curl_slist_sort_insert(requestHeaders, "x-cos-acl", S3fsCurl::default_acl.c_str());
  if(REDUCED_REDUNDANCY == GetStorageClass()){
    requestHeaders = curl_slist_sort_insert(requestHeaders, "x-cos-storage-class", "REDUCED_REDUNDANCY");
  } else if(STANDARD_IA == GetStorageClass()){
    requestHeaders = curl_slist_sort_insert(requestHeaders, "x-cos-storage-class", "STANDARD_IA");
  }

  string date        = get_date_rfc850();
  string ContentType = S3fsCurl::LookupMimeType(string(tpath));
  requestHeaders     = curl_slist_sort_insert(requestHeaders, "Host", host.c_str());
  requestHeaders     = curl_slist_sort_insert(requestHeaders, "Date", date.c_str());
  requestHeaders     = curl_slist_sort_insert(requestHeaders, "Content-Type", ContentType.c_str());
  requestHeaders     = curl_slist_sort_insert(requestHeaders, "Content-Length", "0");

  if(!S3fsCurl::IsPublicBucket()){
	  string Signature = CalcSignature("PUT", "", ContentType, date, resource, "");
	  requestHeaders   = curl_slist_sort_insert(requestHeaders, "Authorization", Signature.c_str());
  }

  // setopt
  curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(hCurl, CURLOPT_UPLOAD, true);                // HTTP PUT
  curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)bodydata);
  curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(hCurl, CURLOPT_INFILESIZE, 0);               // Content-Length
  curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);

  type = REQTYPE_PUTHEAD;

  S3FS_PRN_INFO3("copying... [path=%s]", tpath);

  int result = RequestPerform();
  delete bodydata;
  bodydata = NULL;

  return result;
}

int S3fsCurl::PutRequest(const char* tpath, headers_t& meta, int fd)
{
  struct stat st;
  FILE*       file = NULL;
  int         fd2;

  S3FS_PRN_INFO3("[tpath=%s]", SAFESTRPTR(tpath));

  if(!tpath){
    return -1;
  }
  if(-1 != fd){
    // duplicate fd
    if(-1 == (fd2 = dup(fd)) || -1 == fstat(fd2, &st) || 0 != lseek(fd2, 0, SEEK_SET) || NULL == (file = fdopen(fd2, "rb"))){
      S3FS_PRN_ERR("Could not duplicate file descriptor(errno=%d)", errno);
      if(-1 != fd2){
        close(fd2);
      }
      return -errno;
    }
    b_infile = file;
  }else{
    // This case is creating zero byte obejct.(calling by create_file_object())
    S3FS_PRN_INFO3("create zero byte file object.");
  }

  if(!CreateCurlHandle(true)){
    if(file){
      fclose(file);
    }
    return -1;
  }
  string resource;
  string turl;
  string host;
  MakeUrlResource(get_realpath(tpath).c_str(), resource, turl);

  url             = prepare_url(turl.c_str(), host);
  path            = get_realpath(tpath);
  requestHeaders  = NULL;
  responseHeaders.clear();
  bodydata        = new BodyData();

  // Make request headers
  string strMD5;
  if(-1 != fd && S3fsCurl::is_content_md5){
    strMD5         = s3fs_get_content_md5(fd);
    requestHeaders = curl_slist_sort_insert(requestHeaders, "Content-MD5", strMD5.c_str());
  }

  for(headers_t::iterator iter = meta.begin(); iter != meta.end(); ++iter){
    string key   = lower(iter->first);
    string value = iter->second;
    if(key.substr(0, 9) == "x-cos-acl"){
      // not set value, but after set it.
    }else if(key.substr(0, 10) == "x-cos-meta"){
      requestHeaders = curl_slist_sort_insert(requestHeaders, iter->first.c_str(), value.c_str());
    }
  }
  // "x-cos-acl", storage class, sse
  // requestHeaders = curl_slist_sort_insert(requestHeaders, "x-cos-acl", S3fsCurl::default_acl.c_str());
  if(REDUCED_REDUNDANCY == GetStorageClass()){
    requestHeaders = curl_slist_sort_insert(requestHeaders, "x-cos-storage-class", "REDUCED_REDUNDANCY");
  } else if(STANDARD_IA == GetStorageClass()){
    requestHeaders = curl_slist_sort_insert(requestHeaders, "x-cos-storage-class", "STANDARD_IA");
  }

  string date        = get_date_rfc850();
  string ContentType = S3fsCurl::LookupMimeType(string(tpath));
  requestHeaders     = curl_slist_sort_insert(requestHeaders, "Host", host.c_str());
  requestHeaders     = curl_slist_sort_insert(requestHeaders, "Date", date.c_str());
  requestHeaders     = curl_slist_sort_insert(requestHeaders, "Content-Type", ContentType.c_str());

  if(file) {
    requestHeaders     = curl_slist_sort_insert(requestHeaders, "Content-Length", str(st.st_size).c_str());
  } else {
    requestHeaders     = curl_slist_sort_insert(requestHeaders, "Content-Length", "0");
  }
  if(!S3fsCurl::IsPublicBucket()){
	  string Signature = CalcSignature("PUT", strMD5, ContentType, date, resource, "");
	  requestHeaders   = curl_slist_sort_insert(requestHeaders, "Authorization",Signature.c_str());
  }

  // setopt
  curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(hCurl, CURLOPT_UPLOAD, true);                // HTTP PUT
  curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)bodydata);
  curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);
  if(file){
    curl_easy_setopt(hCurl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(st.st_size)); // Content-Length
    curl_easy_setopt(hCurl, CURLOPT_INFILE, file);
  }else{
    curl_easy_setopt(hCurl, CURLOPT_INFILESIZE, 0);             // Content-Length: 0
  }

  type = REQTYPE_PUT;

  S3FS_PRN_INFO3("uploading... [path=%s][fd=%d][size=%jd]", tpath, fd, (intmax_t)(-1 != fd ? st.st_size : 0));

  int result = RequestPerform();
  delete bodydata;
  bodydata = NULL;
  if(file){
    fclose(file);
  }

  return result;
}

int S3fsCurl::PreGetObjectRequest(const char* tpath, int fd, off_t start, ssize_t size, sse_type_t ssetype, string& ssevalue)
{
  S3FS_PRN_INFO3("[tpath=%s][start=%jd][size=%zd]", SAFESTRPTR(tpath), (intmax_t)start, size);

  if(!tpath || -1 == fd || 0 > start || 0 > size){
    return -1;
  }

  if(!CreateCurlHandle(true)){
    return -1;
  }
  string resource;
  string turl;
  string host;
  MakeUrlResource(get_realpath(tpath).c_str(), resource, turl);

  url             = prepare_url(turl.c_str(), host);
  path            = get_realpath(tpath);
  requestHeaders  = NULL;
  responseHeaders.clear();

  if(-1 != start && 0 < size){
    string range = "bytes=";
    range       += str(start);
    range       += "-";
    range       += str(start + size - 1);
    requestHeaders = curl_slist_sort_insert(requestHeaders, "Range", range.c_str());
  }


  string date    = get_date_rfc850();
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Host", host.c_str());
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Date", date.c_str());
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Content-Type", NULL);

  if(!S3fsCurl::IsPublicBucket()){
	  string Signature = CalcSignature("GET", "", "", date, resource, "");
	  requestHeaders   = curl_slist_sort_insert(requestHeaders, "Authorization", Signature.c_str());
  }

  // setopt
  curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);
  curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, S3fsCurl::DownloadWriteCallback);
  curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)this);

  // set info for callback func.
  // (use only fd, startpos and size, other member is not used.)
  partdata.clear();
  partdata.fd         = fd;
  partdata.startpos   = start;
  partdata.size       = size;
  b_partdata_startpos = start;
  b_partdata_size     = size;
  b_ssetype           = ssetype;
  b_ssevalue          = ssevalue;
  b_ssekey_pos        = -1;         // not use this value for get object.

  type = REQTYPE_GET;

  return 0;
}

int S3fsCurl::GetObjectRequest(const char* tpath, int fd, off_t start, ssize_t size)
{
  int result;

  S3FS_PRN_INFO3("[tpath=%s][start=%jd][size=%zd]", SAFESTRPTR(tpath), (intmax_t)start, size);

  if(!tpath){
    return -1;
  }
  sse_type_t ssetype;
  string     ssevalue;

  if(0 != (result = PreGetObjectRequest(tpath, fd, start, size, ssetype, ssevalue))){
    return result;
  }

  S3FS_PRN_INFO3("downloading... [path=%s][fd=%d]", tpath, fd);

  result = RequestPerform();
  partdata.clear();

  return result;
}

int S3fsCurl::CheckBucket(void)
{
  S3FS_PRN_INFO3("check a bucket.");

  if(!CreateCurlHandle(true)){
    return -1;
  }
  string resource;
  string turl;
  string host;
  MakeUrlResource("/", resource, turl);

  url             = prepare_url(turl.c_str(), host);
  path            = "/";
  requestHeaders  = NULL;
  responseHeaders.clear();
  bodydata        = new BodyData();

  string date    = get_date_rfc850();
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Host", host.c_str());
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Date", date.c_str());

  if(!S3fsCurl::IsPublicBucket()){
	  string Signature = CalcSignature("GET", "", "", date, resource, "");
	  requestHeaders   = curl_slist_sort_insert(requestHeaders, "Authorization", Signature.c_str());
  }

  // setopt
  curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
  //XXX
  //curl_easy_setopt(hCurl, CURLOPT_FAILONERROR, true);
  curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)bodydata);
  curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);

  type = REQTYPE_CHKBUCKET;

  int result = RequestPerform();
  if (result != 0) {
    S3FS_PRN_ERR("Check bucket failed, COS response: %s", (bodydata ? bodydata->str() : ""));
  }
  return result;
}

int S3fsCurl::ListBucketRequest(const char* tpath, const char* query)
{
  S3FS_PRN_INFO3("[tpath=%s]", SAFESTRPTR(tpath));

  if(!tpath){
    return -1;
  }
  if(!CreateCurlHandle(true)){
    return -1;
  }
  string resource;
  string turl;
  string host;
  MakeUrlResource("", resource, turl);    // NOTICE: path is "".
  if(query){
    turl += "?";
    turl += query;
  }

  url             = prepare_url(turl.c_str(), host);
  path            = get_realpath(tpath);
  requestHeaders  = NULL;
  responseHeaders.clear();
  bodydata        = new BodyData();

  string date    = get_date_rfc850();
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Host", host.c_str());
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Date", date.c_str());
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Content-Type", NULL);

  if(!S3fsCurl::IsPublicBucket()){
	  // query has been encoded
	  string Signature = CalcSignature("GET", "", "", date, resource, urlDecode(query));
	  requestHeaders   = curl_slist_sort_insert(requestHeaders, "Authorization", Signature.c_str());
  }

  // setopt
  curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)bodydata);
  curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);

  type = REQTYPE_LISTBUCKET;

  return RequestPerform();
}

//
// Initialize multipart upload
//
// Example :
//   POST /example-object?uploads HTTP/1.1
//   Host: <BucketName>-<UID>.<Region>.myqcloud.com
//   Date: Mon, 1 Nov 2010 20:34:56 GMT
//   Authorization: OSS VGhpcyBtZXNzYWdlIHNpZ25lZCBieSBlbHZpbmc=
//
int S3fsCurl::PreMultipartPostRequest(const char* tpath, headers_t& meta, string& upload_id, bool is_copy)
{
  S3FS_PRN_INFO("[tpath=%s]", SAFESTRPTR(tpath));

  S3FS_PRN_ERR("PreMultipartPostRequest");
  if(!tpath){
    return -1;
  }
  if(!CreateCurlHandle(true)){
    return -1;
  }
  S3FS_PRN_ERR("PreMultipartPostRequest1");
  string resource;
  string turl;
  string host;
  MakeUrlResource(get_realpath(tpath).c_str(), resource, turl);

  string query_string = "uploads";
  //XXX
  //query_string += "=";
  turl          += "?" + query_string;
  // resource      += "?" + query_string;
  url            = prepare_url(turl.c_str(), host);
  path           = get_realpath(tpath);
  requestHeaders = NULL;
  bodydata       = new BodyData();
  responseHeaders.clear();

  string contype = S3fsCurl::LookupMimeType(string(tpath));

  for(headers_t::iterator iter = meta.begin(); iter != meta.end(); ++iter){
    string key   = lower(iter->first);
    string value = iter->second;
    if(key.substr(0, 9) == "x-cos-acl"){
      // not set value, but after set it.
    }else if(key.substr(0, 10) == "x-cos-meta"){
      //carrying object meta information
      requestHeaders = curl_slist_sort_insert(requestHeaders, iter->first.c_str(), value.c_str());
    }
  }
  // "x-cos-acl", storage class, sse
  /*requestHeaders = curl_slist_sort_insert(requestHeaders, "x-cos-acl", S3fsCurl::default_acl.c_str());
  if(REDUCED_REDUNDANCY == GetStorageClass()){
    requestHeaders = curl_slist_sort_insert(requestHeaders, "x-cos-storage-class", "REDUCED_REDUNDANCY");
  } else if(STANDARD_IA == GetStorageClass()){
    requestHeaders = curl_slist_sort_insert(requestHeaders, "x-cos-storage-class", "STANDARD_IA");
  }*/


  string date    = get_date_rfc850();
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Host", host.c_str());
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Date", date.c_str());
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Accept", NULL);
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Content-Length", "0");
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Content-Type", contype.c_str());

  if(!S3fsCurl::IsPublicBucket()){
	  string Signature = CalcSignature("POST", "", contype, date, resource, query_string);
	  requestHeaders   = curl_slist_sort_insert(requestHeaders, "Authorization",  Signature.c_str());
  }

  // setopt
  curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(hCurl, CURLOPT_POST, true);              // POST
  curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)bodydata);
  curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(hCurl, CURLOPT_POSTFIELDSIZE, 0);
  curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);

  type = REQTYPE_PREMULTIPOST;

  // request
  int result;
  if(0 != (result = RequestPerform())){
    delete bodydata;
    bodydata = NULL;
    return result;
  }

  S3FS_PRN_ERR("PreMultipartPostRequest3");
  // Parse XML body for UploadId
  if(!S3fsCurl::GetUploadId(upload_id)){
    delete bodydata;
    bodydata = NULL;
    return -1;
  }

  delete bodydata;
  bodydata = NULL;
  return 0;
}

int S3fsCurl::CompleteMultipartPostRequest(const char* tpath, string& upload_id, etaglist_t& parts)
{
  S3FS_PRN_INFO3("[tpath=%s][parts=%zu]", SAFESTRPTR(tpath), parts.size());

  if(!tpath){
    return -1;
  }

  // make contents
  string postContent;
  postContent += "<CompleteMultipartUpload>\n";
  for(int cnt = 0; cnt < (int)parts.size(); cnt++){
    if(0 == parts[cnt].length()){
      S3FS_PRN_ERR("%d file part is not finished uploading.", cnt + 1);
      return -1;
    }
    postContent += "<Part>\n";
    postContent += "  <PartNumber>" + str(cnt + 1) + "</PartNumber>\n";
    postContent += "  <ETag>\""     + parts[cnt]   + "\"</ETag>\n";
    postContent += "</Part>\n";
  }
  postContent += "</CompleteMultipartUpload>\n";

  // set postdata
  postdata             = reinterpret_cast<const unsigned char*>(postContent.c_str());
  b_postdata           = postdata;
  postdata_remaining   = postContent.size(); // without null
  b_postdata_remaining = postdata_remaining;

  if(!CreateCurlHandle(true)){
    return -1;
  }
  string resource;
  string turl;
  string host;
  MakeUrlResource(get_realpath(tpath).c_str(), resource, turl);

  string query_string  = "uploadId=" + upload_id;
  turl                += "?" + query_string;
  // resource            += "?" + query_string;
  url                  = prepare_url(turl.c_str(), host);
  path                 = get_realpath(tpath);
  requestHeaders       = NULL;
  bodydata             = new BodyData();
  responseHeaders.clear();
  string contype       = "application/xml";

  string date    = get_date_rfc850();
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Host", host.c_str());
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Date", date.c_str());
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Accept", NULL);
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Content-Type", contype.c_str());
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Content-Length", str(postdata_remaining).c_str());

  if(!S3fsCurl::IsPublicBucket()){
	  string Signature = CalcSignature("POST", "", contype, date, resource, query_string);
	  requestHeaders   = curl_slist_sort_insert(requestHeaders, "Authorization", Signature.c_str());
  }

  // setopt
  curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);
  curl_easy_setopt(hCurl, CURLOPT_POST, true);              // POST
  curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)bodydata);
  curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(hCurl, CURLOPT_POSTFIELDSIZE, static_cast<curl_off_t>(postdata_remaining));
  curl_easy_setopt(hCurl, CURLOPT_READDATA, (void*)this);
  curl_easy_setopt(hCurl, CURLOPT_READFUNCTION, S3fsCurl::ReadCallback);

  type = REQTYPE_COMPLETEMULTIPOST;

  // request
  int result = RequestPerform();
  delete bodydata;
  bodydata = NULL;
  postdata = NULL;

  return result;
}

int S3fsCurl::MultipartListRequest(string& body)
{
  S3FS_PRN_INFO3("list request(multipart)");

  if(!CreateCurlHandle(true)){
    return -1;
  }
  string resource;
  string turl;
  string host;
  string query_string;
  path            = get_realpath("/");
  MakeUrlResource(path.c_str(), resource, turl);

  query_string    = "uploads";
  turl           += "?" + query_string;
  resource       += "?" + query_string;
  url             = prepare_url(turl.c_str(), host);
  requestHeaders  = NULL;
  responseHeaders.clear();
  bodydata        = new BodyData();

  string date    = get_date_rfc850();
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Host", host.c_str());
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Date", date.c_str());
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Accept", NULL);

  if(!S3fsCurl::IsPublicBucket()){
	  string Signature = CalcSignature("GET", "", "", date, resource, query_string);
	  requestHeaders   = curl_slist_sort_insert(requestHeaders, "Authorization", Signature.c_str());
  }

  // setopt
  curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)bodydata);
  curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);

  type = REQTYPE_MULTILIST;

  int result;
  if(0 == (result = RequestPerform()) && 0 < bodydata->size()){
    body = bodydata->str();
  }else{
    body = "";
  }
  delete bodydata;
  bodydata = NULL;

  return result;
}

int S3fsCurl::AbortMultipartUpload(const char* tpath, string& upload_id)
{
  S3FS_PRN_INFO3("[tpath=%s]", SAFESTRPTR(tpath));

  if(!tpath){
    return -1;
  }
  if(!CreateCurlHandle(true)){
    return -1;
  }
  string resource;
  string turl;
  string host;
  string query_string;
  MakeUrlResource(get_realpath(tpath).c_str(), resource, turl);

  query_string    = "uploadId=" + upload_id;
  turl           += "?" + query_string;
  resource       += "?" + query_string;
  url             = prepare_url(turl.c_str(), host);
  path            = get_realpath(tpath);
  requestHeaders  = NULL;
  responseHeaders.clear();

  string date    = get_date_rfc850();
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Host", host.c_str());
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Date", date.c_str());

  if(!S3fsCurl::IsPublicBucket()){
	  string Signature = CalcSignature("DELETE", "", "", date, resource, query_string);
	  requestHeaders   = curl_slist_sort_insert(requestHeaders, "Authorization", Signature.c_str());
  }

  curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(hCurl, CURLOPT_CUSTOMREQUEST, "DELETE");
  curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);

  type = REQTYPE_ABORTMULTIUPLOAD;

  return RequestPerform();
}

//
// PUT /ObjectName?partNumber=PartNumber&uploadId=UploadId HTTP/1.1
// Host: BucketName-Appid.cn-south.myqcloud.com
// Date: date
// Content-Length: Size
// Authorization: Signature
//
// PUT /my-movie.m2ts?partNumber=1&uploadId=VCVsb2FkIElEIGZvciBlbZZpbmcncyBteS1tb3ZpZS5tMnRzIHVwbG9hZR HTTP/1.1
// Host: BucketName-Appid.cn-south.myqcloud.com
// Date:  Mon, 1 Nov 2010 20:34:56 GMT
// Content-Length: 10485760
// Content-MD5: pUNXr/BjKK5G2UKvaRRrOA==
//

int S3fsCurl::UploadMultipartPostSetup(const char* tpath, int part_num, string& upload_id)
{
  S3FS_PRN_INFO3("[tpath=%s][start=%jd][size=%zd][part=%d]", SAFESTRPTR(tpath), (intmax_t)(partdata.startpos), partdata.size, part_num);

  if(-1 == partdata.fd || -1 == partdata.startpos || -1 == partdata.size){
    return -1;
  }

  // make sha1 and file pointer
  // unsigned char *md5raw = s3fs_md5hexsum(partdata.fd, partdata.startpos, partdata.size);
  // if(md5raw == NULL){
  //   S3FS_PRN_ERR("Could not make md5 for file(part %d)", part_num);
  //   return -1;
  // }
  // partdata.etag = s3fs_hex(md5raw, get_md5_digest_length());
  // char* md5base64p = s3fs_base64(md5raw, get_md5_digest_length());
  std::string md5base64; //  = md5base64p;
  // free(md5base64p);
  // free(md5raw);

  // create handle
  if(!CreateCurlHandle(true)){
    return -1;
  }

  // make request
  string request_uri = "partNumber=" + str(part_num) + "&uploadId=" + upload_id;
  string urlargs     = "?" + request_uri;
  string resource;
  string turl;
  string host;
  MakeUrlResource(get_realpath(tpath).c_str(), resource, turl);

  turl              += urlargs;
  url                = prepare_url(turl.c_str(), host);
  path               = get_realpath(tpath);
  requestHeaders     = NULL;
  bodydata           = new BodyData();
  headdata           = new BodyData();
  responseHeaders.clear();

  string date    = get_date_rfc850();
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Host", host.c_str());
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Date", date.c_str());
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Accept", NULL);

  string strMD5;
  if(S3fsCurl::is_content_md5){
     unsigned char *md5raw = s3fs_md5_fd(partdata.fd, partdata.startpos, partdata.size);
      if(md5raw == NULL){
          S3FS_PRN_ERR("Could not make md5 for file(part %d)", part_num);
          return -EIO;
      }
      partdata.etag = s3fs_hex_lower(md5raw, get_md5_digest_length());
      char* md5base64p = s3fs_base64(md5raw, get_md5_digest_length());
      requestHeaders = curl_slist_sort_insert(requestHeaders, "Content-MD5", md5base64p);
      delete[] md5base64p;
      delete[] md5raw;
  }
  requestHeaders = curl_slist_sort_insert(requestHeaders, "Content-Length", str(partdata.size).c_str());

  if(!S3fsCurl::IsPublicBucket()){
	  string Signature = CalcSignature("PUT", strMD5, "", date, resource, request_uri);
	  requestHeaders   = curl_slist_sort_insert(requestHeaders, "Authorization", Signature.c_str());
  }

  // setopt
  curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(hCurl, CURLOPT_UPLOAD, true);              // HTTP PUT
  curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)bodydata);
  curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(hCurl, CURLOPT_HEADERDATA, (void*)headdata);
  curl_easy_setopt(hCurl, CURLOPT_HEADERFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(hCurl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(partdata.size)); // Content-Length
  curl_easy_setopt(hCurl, CURLOPT_READFUNCTION, S3fsCurl::UploadReadCallback);
  curl_easy_setopt(hCurl, CURLOPT_READDATA, (void*)this);
  curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);

  type = REQTYPE_UPLOADMULTIPOST;

  return 0;
}

int S3fsCurl::UploadMultipartPostRequest(const char* tpath, int part_num, string& upload_id)
{
  int result;

  S3FS_PRN_INFO3("[tpath=%s][start=%jd][size=%zd][part=%d]", SAFESTRPTR(tpath), (intmax_t)(partdata.startpos), partdata.size, part_num);

  // setup
  if(0 != (result = S3fsCurl::UploadMultipartPostSetup(tpath, part_num, upload_id))){
    return result;
  }

  // request
  if(0 == (result = RequestPerform())){
    // check etag
	// cos's  no check etag
    // if(NULL != strstr(headdata->str(), upper(partdata.etag).c_str())){
      // get etag from response header
      S3FS_PRN_ERR("headdata is : %s", headdata->str());
      string header_str(headdata->str(), headdata->size());
      int pos = header_str.find("ETag: \"");
      if (pos != std::string::npos) {
          if (header_str.at(pos+39) != '"') {
              // sha1
              partdata.etag = header_str.substr(pos + 7, 40);
          } else {
              partdata.etag = header_str.substr(pos + 7, 32);  // ETag get md5 value
          }
          S3FS_PRN_ERR("partdata.etag : %s", partdata.etag.c_str());
      }
      partdata.uploaded = true;
    // }else{
    //   result = -1;
    // }
  }

  // closing
  delete bodydata;
  bodydata = NULL;
  delete headdata;
  headdata = NULL;

  return result;
}

int S3fsCurl::CopyMultipartPostRequest(const char* from, const char* to, int part_num, string& upload_id, headers_t& meta)
{
  S3FS_PRN_INFO3("[from=%s][to=%s][part=%d]", SAFESTRPTR(from), SAFESTRPTR(to), part_num);

  if(!from || !to){
    return -1;
  }
  if(!CreateCurlHandle(true)){
    return -1;
  }
  string urlargs  = "partNumber=" + str(part_num) + "&uploadId=" + upload_id;
  string resource;
  string turl;
  string host;
  MakeUrlResource(get_realpath(to).c_str(), resource, turl);

  turl           += "?" + urlargs;
  url             = prepare_url(turl.c_str(), host);
  path            = get_realpath(to);
  requestHeaders  = NULL;
  responseHeaders.clear();
  bodydata        = new BodyData();
  headdata        = new BodyData();

  // Make request headers
  for(headers_t::iterator iter = meta.begin(); iter != meta.end(); ++iter){
    string key   = lower(iter->first);
    string value = iter->second;
    if(key == "x-cos-copy-source"){
      requestHeaders = curl_slist_sort_insert(requestHeaders, iter->first.c_str(), value.c_str());
    }else if(key == "x-cos-copy-source-range"){
      requestHeaders = curl_slist_sort_insert(requestHeaders, iter->first.c_str(), value.c_str());
    } 
    // NOTICE: x-cos-acl, x-cos-server-side-encryption is not set!
  }

  string date        = get_date_rfc850();
  string ContentType = S3fsCurl::LookupMimeType(string(to));
  requestHeaders	 = curl_slist_sort_insert(requestHeaders, "Host", host.c_str());
  requestHeaders     = curl_slist_sort_insert(requestHeaders, "Date", date.c_str());
  requestHeaders     = curl_slist_sort_insert(requestHeaders, "Content-Type", ContentType.c_str());
  requestHeaders     = curl_slist_sort_insert(requestHeaders, "Content-Length", "0");

  if(!S3fsCurl::IsPublicBucket()){
	  string Signature = CalcSignature("PUT", "", ContentType, date, resource, urlargs);
	  requestHeaders   = curl_slist_sort_insert(requestHeaders, "Authorization", Signature.c_str());
  }

  // setopt
  curl_easy_setopt(hCurl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(hCurl, CURLOPT_UPLOAD, true);                // HTTP PUT
  curl_easy_setopt(hCurl, CURLOPT_WRITEDATA, (void*)bodydata);
  curl_easy_setopt(hCurl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(hCurl, CURLOPT_HEADERDATA, (void*)headdata);
  curl_easy_setopt(hCurl, CURLOPT_HEADERFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(hCurl, CURLOPT_INFILESIZE, 0);               // Content-Length
  curl_easy_setopt(hCurl, CURLOPT_HTTPHEADER, requestHeaders);

  type = REQTYPE_COPYMULTIPOST;

  // request
  S3FS_PRN_INFO3("copying... [from=%s][to=%s][part=%d]", from, to, part_num);

  int result = RequestPerform();
  if(0 == result){
    // parse ETag from response
    xmlDocPtr doc;
    if(NULL == (doc = xmlReadMemory(bodydata->str(), bodydata->size(), "", NULL, 0))){
      return result;
    }
    if(NULL == doc->children){
      S3FS_XMLFREEDOC(doc);
      return result;
    }
    for(xmlNodePtr cur_node = doc->children->children; NULL != cur_node; cur_node = cur_node->next){
      if(XML_ELEMENT_NODE == cur_node->type){
        string elementName = reinterpret_cast<const char*>(cur_node->name);
        if(cur_node->children){
          if(XML_TEXT_NODE == cur_node->children->type){
            if(elementName == "ETag") {
              string etag = reinterpret_cast<const char *>(cur_node->children->content);
              if(etag.size() >= 2 && *etag.begin() == '"' && *etag.rbegin() == '"'){
                etag.assign(etag.substr(1, etag.size() - 2));
              }
              partdata.etag.assign(etag);
              partdata.uploaded = true;
            }
          }
        }
      }
    }
    S3FS_XMLFREEDOC(doc);
  }

  delete bodydata;
  bodydata = NULL;
  delete headdata;
  headdata = NULL;

  return result;
}

int S3fsCurl::MultipartHeadRequest(const char* tpath, off_t size, headers_t& meta, bool is_copy)
{
  int            result;
  string         upload_id;
  off_t          chunk;
  off_t          bytes_remaining;
  etaglist_t     list;
  stringstream   strrange;

  S3FS_PRN_INFO3("[tpath=%s]", SAFESTRPTR(tpath));

  if(0 != (result = PreMultipartPostRequest(tpath, meta, upload_id, is_copy))){
    return result;
  }
  DestroyCurlHandle();

  for(bytes_remaining = size, chunk = 0; 0 < bytes_remaining; bytes_remaining -= chunk){
    chunk = bytes_remaining > MAX_MULTI_COPY_SOURCE_SIZE ? MAX_MULTI_COPY_SOURCE_SIZE : bytes_remaining;

    strrange << "bytes=" << (size - bytes_remaining) << "-" << (size - bytes_remaining + chunk - 1);
    meta["x-cos-copy-source-range"] = strrange.str();
    strrange.str("");
    strrange.clear(stringstream::goodbit);

    if(0 != (result = CopyMultipartPostRequest(tpath, tpath, (list.size() + 1), upload_id, meta))){
      return result;
    }
    list.push_back(partdata.etag);
    DestroyCurlHandle();
  }

  if(0 != (result = CompleteMultipartPostRequest(tpath, upload_id, list))){
    return result;
  }
  return 0;
}

int S3fsCurl::MultipartUploadRequest(const char* tpath, headers_t& meta, int fd, bool is_copy)
{
  int            result;
  string         upload_id;
  struct stat    st;
  int            fd2;
  etaglist_t     list;
  off_t          remaining_bytes;
  off_t          chunk;

  S3FS_PRN_INFO3("[tpath=%s][fd=%d]", SAFESTRPTR(tpath), fd);

  // duplicate fd
  if(-1 == (fd2 = dup(fd)) || 0 != lseek(fd2, 0, SEEK_SET)){
    S3FS_PRN_ERR("Could not duplicate file descriptor(errno=%d)", errno);
    if(-1 != fd2){
      close(fd2);
    }
    return -errno;
  }
  if(-1 == fstat(fd2, &st)){
    S3FS_PRN_ERR("Invalid file descriptor(errno=%d)", errno);
    close(fd2);
    return -errno;
  }

  if(0 != (result = PreMultipartPostRequest(tpath, meta, upload_id, is_copy))){
    close(fd2);
    return result;
  }
  DestroyCurlHandle();

  // cycle through open fd, pulling off 10MB chunks at a time
  for(remaining_bytes = st.st_size; 0 < remaining_bytes; remaining_bytes -= chunk){
    // chunk size
    chunk = remaining_bytes > S3fsCurl::multipart_size ? S3fsCurl::multipart_size : remaining_bytes;

    // set
    partdata.fd         = fd2;
    partdata.startpos   = st.st_size - remaining_bytes;
    partdata.size       = chunk;
    b_partdata_startpos = partdata.startpos;
    b_partdata_size     = partdata.size;

    // upload part
    if(0 != (result = UploadMultipartPostRequest(tpath, (list.size() + 1), upload_id))){
      S3FS_PRN_ERR("failed uploading part(%d)", result);
      close(fd2);
      return result;
    }

    list.push_back(partdata.etag);
    DestroyCurlHandle();
  }
  close(fd2);

  if(0 != (result = CompleteMultipartPostRequest(tpath, upload_id, list))){
    return result;
  }
  return 0;
}

int S3fsCurl::MultipartUploadRequest(string upload_id, const char* tpath, int fd, off_t offset, size_t size, etaglist_t& list)
{
  S3FS_PRN_INFO3("[upload_id=%s][tpath=%s][fd=%d][offset=%jd][size=%jd]", upload_id.c_str(), SAFESTRPTR(tpath), fd, (intmax_t)offset, (intmax_t)size);

  // duplicate fd
  int fd2;
  if(-1 == (fd2 = dup(fd)) || 0 != lseek(fd2, 0, SEEK_SET)){
    S3FS_PRN_ERR("Could not duplicate file descriptor(errno=%d)", errno);
    if(-1 != fd2){
      close(fd2);
    }
    return -errno;
  }

  // set
  partdata.fd         = fd2;
  partdata.startpos   = offset;
  partdata.size       = size;
  b_partdata_startpos = partdata.startpos;
  b_partdata_size     = partdata.size;

  // upload part
  int   result;
  if(0 != (result = UploadMultipartPostRequest(tpath, (list.size() + 1), upload_id))){
    S3FS_PRN_ERR("failed uploading part(%d)", result);
    close(fd2);
    return result;
  }
  list.push_back(partdata.etag);
  DestroyCurlHandle();
  close(fd2);

  return 0;
}

int S3fsCurl::MultipartRenameRequest(const char* from, const char* to, headers_t& meta, off_t size)
{
  int            result;
  string         upload_id;
  off_t          chunk;
  off_t          bytes_remaining;
  etaglist_t     list;
  stringstream   strrange;

  S3FS_PRN_INFO3("[from=%s][to=%s]", SAFESTRPTR(from), SAFESTRPTR(to));

  string srcresource;
  string srcurl;
  MakeUrlResource(get_realpath(from).c_str(), srcresource, srcurl);

  meta["Content-Type"]      = S3fsCurl::LookupMimeType(string(to));
  meta["x-cos-copy-source"] = urlEncode(service_path + bucket + "-" + appid + get_realpath(from));

  if(0 != (result = PreMultipartPostRequest(to, meta, upload_id, true))){
    return result;
  }
  DestroyCurlHandle();

  for(bytes_remaining = size, chunk = 0; 0 < bytes_remaining; bytes_remaining -= chunk){
    chunk = bytes_remaining > MAX_MULTI_COPY_SOURCE_SIZE ? MAX_MULTI_COPY_SOURCE_SIZE : bytes_remaining;

    strrange << "bytes=" << (size - bytes_remaining) << "-" << (size - bytes_remaining + chunk - 1);
    meta["x-cos-copy-source-range"] = strrange.str();
    strrange.str("");
    strrange.clear(stringstream::goodbit);

    if(0 != (result = CopyMultipartPostRequest(from, to, (list.size() + 1), upload_id, meta))){
      return result;
    }
    list.push_back(partdata.etag);
    DestroyCurlHandle();
  }

  if(0 != (result = CompleteMultipartPostRequest(to, upload_id, list))){
    return result;
  }
  return 0;
}

std::string S3fsCurl::GetClientInfo(std::string pid)
{
  if (pid == "-1") {
    return "can not get pid";
  }
  std::string path = "/proc/" + pid + "/cgroup";
  std::ifstream ifs(path.c_str());
  if (!ifs) {
    S3FS_PRN_ERR("failed to open %s", path.c_str());
    return "failed to open " + path;
  }
  std::string line;
  while (std::getline(ifs, line)) {
    if (line.find("cpu,cpuacct") != std::string::npos) {
      return line;
    }
    if (line.find("cpuacct,cpu") != std::string::npos) {
      return line;
    }
  }
  return "not found cpu,cpuacct";
}

//-------------------------------------------------------------------
// Class S3fsMultiCurl
//-------------------------------------------------------------------
#define MAX_MULTI_HEADREQ   20   // default: max request count in readdir curl_multi.

//-------------------------------------------------------------------
// Class method for S3fsMultiCurl
//-------------------------------------------------------------------
int S3fsMultiCurl::max_multireq = MAX_MULTI_HEADREQ;

int S3fsMultiCurl::SetMaxMultiRequest(int max)
{
  int old = S3fsMultiCurl::max_multireq;
  S3fsMultiCurl::max_multireq= max;
  return old;
}

//-------------------------------------------------------------------
// method for S3fsMultiCurl
//-------------------------------------------------------------------
S3fsMultiCurl::S3fsMultiCurl() : SuccessCallback(NULL), RetryCallback(NULL)
{
}

S3fsMultiCurl::~S3fsMultiCurl()
{
  Clear();
}

bool S3fsMultiCurl::ClearEx(bool is_all)
{
  s3fscurlmap_t::iterator iter;
  for(iter = cMap_req.begin(); iter != cMap_req.end(); cMap_req.erase(iter++)){
    S3fsCurl* s3fscurl = (*iter).second;
    if(s3fscurl){
      s3fscurl->DestroyCurlHandle();
      delete s3fscurl;  // with destroy curl handle.
    }
  }

  if(is_all){
    for(iter = cMap_all.begin(); iter != cMap_all.end(); cMap_all.erase(iter++)){
      S3fsCurl* s3fscurl = (*iter).second;
      s3fscurl->DestroyCurlHandle();
      delete s3fscurl;
    }
  }
  S3FS_MALLOCTRIM(0);

  return true;
}

S3fsMultiSuccessCallback S3fsMultiCurl::SetSuccessCallback(S3fsMultiSuccessCallback function)
{
  S3fsMultiSuccessCallback old = SuccessCallback;
  SuccessCallback = function;
  return old;
}

S3fsMultiRetryCallback S3fsMultiCurl::SetRetryCallback(S3fsMultiRetryCallback function)
{
  S3fsMultiRetryCallback old = RetryCallback;
  RetryCallback = function;
  return old;
}

bool S3fsMultiCurl::SetS3fsCurlObject(S3fsCurl* s3fscurl)
{
  if(!s3fscurl){
    return false;
  }
  if(cMap_all.end() != cMap_all.find(s3fscurl->hCurl)){
    return false;
  }
  cMap_all[s3fscurl->hCurl] = s3fscurl;
  return true;
}

int S3fsMultiCurl::MultiPerform(void)
{
  std::vector<pthread_t>   threads;
  bool                     success = true;

  for(s3fscurlmap_t::iterator iter = cMap_req.begin(); iter != cMap_req.end(); ++iter) {
    pthread_t   thread;
    S3fsCurl*   s3fscurl = (*iter).second;
    int         rc;

    rc = pthread_create(&thread, NULL, S3fsMultiCurl::RequestPerformWrapper, static_cast<void*>(s3fscurl));
    if (rc != 0) {
      success = false;
      S3FS_PRN_ERR("failed pthread_create - rc(%d)", rc);
      break;
    }

    threads.push_back(thread);
  }

  for (std::vector<pthread_t>::iterator iter = threads.begin(); iter != threads.end(); ++iter) {
    void*   retval;
    int     rc;

    rc = pthread_join(*iter, &retval);
    if (rc) {
      success = false;
      S3FS_PRN_ERR("failed pthread_join - rc(%d)", rc);
    } else {
      int int_retval = (int)(intptr_t)(retval);
      if (int_retval) {
        S3FS_PRN_ERR("thread failed - rc(%d)", int_retval);
        success = false;
      }
    }
  }

  return success ? 0 : -EIO;
}

int S3fsMultiCurl::MultiRead(void)
{
  for(s3fscurlmap_t::iterator iter = cMap_req.begin(); iter != cMap_req.end(); cMap_req.erase(iter++)) {
    S3fsCurl* s3fscurl = (*iter).second;

    bool isRetry = false;

    long responseCode = -1;
    if(s3fscurl->GetResponseCode(responseCode)){
      if(400 > responseCode){
        // add into stat cache
        if(SuccessCallback && !SuccessCallback(s3fscurl)){
          S3FS_PRN_WARN("error from callback function(%s).", s3fscurl->url.c_str());
        }
      }else if(400 == responseCode){
        // as possibly in multipart
        S3FS_PRN_WARN("failed a request(%ld: %s)", responseCode, s3fscurl->url.c_str());
        isRetry = true;
      }else if(404 == responseCode){
        // not found
        S3FS_PRN_WARN("failed a request(%ld: %s)", responseCode, s3fscurl->url.c_str());
      }else if(500 == responseCode){
        // case of all other result, do retry.(11/13/2013)
        // because it was found that s3fs got 500 error from S3, but could success
        // to retry it.
        S3FS_PRN_WARN("failed a request(%ld: %s)", responseCode, s3fscurl->url.c_str());
        isRetry = true;
      }else{
        // Retry in other case.
        S3FS_PRN_WARN("failed a request(%ld: %s)", responseCode, s3fscurl->url.c_str());
        isRetry = true;
      }
    }else{
      S3FS_PRN_ERR("failed a request(Unknown response code: %s)", s3fscurl->url.c_str());
    }


    if(!isRetry){
      s3fscurl->DestroyCurlHandle();
      delete s3fscurl;

    }else{
      S3fsCurl* retrycurl = NULL;

      // For retry
      if(RetryCallback){
        retrycurl = RetryCallback(s3fscurl);
        if(NULL != retrycurl){
          cMap_all[retrycurl->hCurl] = retrycurl;
        }else{
          // Could not set up callback.
          return -EIO;
        }
      }
      if(s3fscurl != retrycurl){
        s3fscurl->DestroyCurlHandle();
        delete s3fscurl;
      }
    }
  }
  return 0;
}

int S3fsMultiCurl::Request(void)
{
  int result;

  S3FS_PRN_INFO3("[count=%zu]", cMap_all.size());

  // Make request list.
  //
  // Send multi request loop( with retry )
  // (When many request is sends, sometimes gets "Couldn't connect to server")
  //
  while(!cMap_all.empty()){
    // set curl handle to multi handle
    int                     cnt;
    s3fscurlmap_t::iterator iter;
    for(cnt = 0, iter = cMap_all.begin(); cnt < S3fsMultiCurl::max_multireq && iter != cMap_all.end(); cMap_all.erase(iter++), cnt++){
      CURL*     hCurl    = (*iter).first;
      S3fsCurl* s3fscurl = (*iter).second;

      cMap_req[hCurl] = s3fscurl;
    }

    // Send multi request.
    if(0 != (result = MultiPerform())){
      Clear();
      return result;
    }

    // Read the result
    if(0 != (result = MultiRead())){
      Clear();
      return result;
    }

    // Cleanup curl handle in multi handle
    ClearEx(false);
  }
  return 0;
}

// thread function for performing an S3fsCurl request
void* S3fsMultiCurl::RequestPerformWrapper(void* arg) {
  return (void*)(intptr_t)(static_cast<S3fsCurl*>(arg)->RequestPerform());
}

//-------------------------------------------------------------------
// Class AdditionalHeader
//-------------------------------------------------------------------
AdditionalHeader AdditionalHeader::singleton;

//-------------------------------------------------------------------
// Class AdditionalHeader method
//-------------------------------------------------------------------
AdditionalHeader::AdditionalHeader()
{
  if(this == AdditionalHeader::get()){
    is_enable = false;
  }else{
    assert(false);
  }
}

AdditionalHeader::~AdditionalHeader()
{
  if(this == AdditionalHeader::get()){
    Unload();
  }else{
    assert(false);
  }
}

bool AdditionalHeader::Load(const char* file)
{
  if(!file){
    S3FS_PRN_WARN("file is NULL.");
    return false;
  }
  Unload();

  ifstream AH(file);
  if(!AH.good()){
    S3FS_PRN_WARN("Could not open file(%s).", file);
    return false;
  }

  // read file
  string line;
  while(getline(AH, line)){
    if('#' == line[0]){
      continue;
    }
    if(0 == line.size()){
      continue;
    }
    // load a line
    stringstream ss(line);
    string       key("");       // suffix(key)
    string       head;          // additional HTTP header
    string       value;         // header value
    if(0 == isblank(line[0])){
      ss >> key;
    }
    if(ss){
      ss >> head;
      if(ss && static_cast<size_t>(ss.tellg()) < line.size()){
        value = line.substr(static_cast<int>(ss.tellg()) + 1);
      }
    }

    // check it
    if(0 == head.size()){
      if(0 == key.size()){
        continue;
      }
      S3FS_PRN_ERR("file format error: %s key(suffix) is no HTTP header value.", key.c_str());
      Unload();
      return false;
    }

    // set charcntlist
    int keylen = key.size();
    charcnt_list_t::iterator iter;
    for(iter = charcntlist.begin(); iter != charcntlist.end(); ++iter){
      if(keylen == (*iter)){
        break;
      }
    }
    if(iter == charcntlist.end()){
      charcntlist.push_back(keylen);
    }
    // set addheader
    addheader_t::iterator aiter;
    if(addheader.end() == (aiter = addheader.find(key))){
      headerpair_t hpair;
      hpair[head]    = value;
      addheader[key] = hpair;
    }else{
      aiter->second[head] = value;
    }
    // set flag
    if(!is_enable){
      is_enable = true;
    }
  }
  return true;
}

void AdditionalHeader::Unload(void)
{
  is_enable = false;
  charcntlist.clear();
  addheader.clear();
}

bool AdditionalHeader::AddHeader(headers_t& meta, const char* path) const
{
  if(!is_enable){
    return true;
  }
  if(!path){
    S3FS_PRN_WARN("path is NULL.");
    return false;
  }
  int nPathLen = strlen(path);
  for(charcnt_list_t::const_iterator iter = charcntlist.begin(); iter != charcntlist.end(); ++iter){
    // get target character count
    if(nPathLen < (*iter)){
      continue;
    }
    // make target suffix(same character count) & find
    string suffix(&path[nPathLen - (*iter)]);
    addheader_t::const_iterator aiter;
    if(addheader.end() == (aiter = addheader.find(suffix))){
      continue;
    }
    for(headerpair_t::const_iterator piter = aiter->second.begin(); piter != aiter->second.end(); ++piter){
      // Adding header
      meta[(*piter).first] = (*piter).second;
    }
  }
  return true;
}

struct curl_slist* AdditionalHeader::AddHeader(struct curl_slist* list, const char* path) const
{
  headers_t meta;

  if(!AddHeader(meta, path)){
    return list;
  }
  for(headers_t::iterator iter = meta.begin(); iter != meta.end(); ++iter){
    // Adding header
    list = curl_slist_sort_insert(list, iter->first.c_str(), iter->second.c_str());
  }
  meta.clear();
  S3FS_MALLOCTRIM(0);
  return list;
}

bool AdditionalHeader::Dump(void) const
{
  if(!IS_S3FS_LOG_DBG()){
    return true;
  }
  // character count list
  stringstream ssdbg;
  ssdbg << "Character count list[" << charcntlist.size() << "] = {";
  for(charcnt_list_t::const_iterator citer = charcntlist.begin(); citer != charcntlist.end(); ++citer){
    ssdbg << " " << (*citer);
  }
  ssdbg << " }\n";

  // additional header
  ssdbg << "Additional Header list[" << addheader.size() << "] = {\n";
  for(addheader_t::const_iterator aiter = addheader.begin(); aiter != addheader.end(); ++aiter){
    string key = (*aiter).first;
    if(0 == key.size()){
      key = "*";
    }
    for(headerpair_t::const_iterator piter = (*aiter).second.begin(); piter != (*aiter).second.end(); ++piter){
      ssdbg << "    " << key << "\t--->\t" << (*piter).first << ": " << (*piter).second << "\n";
    }
  }
  ssdbg << "}";

  // print all
  S3FS_PRN_DBG("%s", ssdbg.str().c_str());

  return true;
}

//-------------------------------------------------------------------
// Utility functions
//-------------------------------------------------------------------
//
// curl_slist_sort_insert
// This function is like curl_slist_append function, but this adds data by a-sorting.
// Because OSS signature needs sorted header.
//
struct curl_slist* curl_slist_sort_insert(struct curl_slist* list, const char* data)
{
  if(!data){
    return list;
  }
  string strkey = data;
  string strval = "";

  string::size_type pos = strkey.find(':', 0);
  if(string::npos != pos){
    strval = strkey.substr(pos + 1);
    strkey = strkey.substr(0, pos);
  }

  return curl_slist_sort_insert(list, strkey.c_str(), strval.c_str());
}

struct curl_slist* curl_slist_sort_insert(struct curl_slist* list, const char* key, const char* value)
{
  struct curl_slist* curpos;
  struct curl_slist* lastpos;
  struct curl_slist* new_item;

  if(!key){
    return list;
  }
  if(NULL == (new_item = (struct curl_slist*)malloc(sizeof(struct curl_slist)))){
    return list;
  }

  // key & value are trimed and lower(only key)
  string strkey = trim(string(key));
  string strval = trim(string(value ? value : ""));
  string strnew = key + string(": ") + strval;
  if(NULL == (new_item->data = strdup(strnew.c_str()))){
    free(new_item);
    return list;
  }
  new_item->next = NULL;

  for(lastpos = NULL, curpos = list; curpos; lastpos = curpos, curpos = curpos->next){
    string strcur = curpos->data;
    size_t pos;
    if(string::npos != (pos = strcur.find(':', 0))){
      strcur = strcur.substr(0, pos);
    }

    int result = strcasecmp(strkey.c_str(), strcur.c_str());
    if(0 == result){
      // same data, so replace it.
      if(lastpos){
        lastpos->next = new_item;
      }else{
        list = new_item;
      }
      new_item->next = curpos->next;
      free(curpos->data);
      free(curpos);
      break;

    }else if(0 > result){
      // add data before curpos.
      if(lastpos){
        lastpos->next = new_item;
      }else{
        list = new_item;
      }
      new_item->next = curpos;
      break;
    }
  }

  if(!curpos){
    // append to last pos
    if(lastpos){
      lastpos->next = new_item;
    }else{
      // a case of list is null
      list = new_item;
    }
  }

  return list;
}


string get_sorted_header_keys(const struct curl_slist* list)
{
  string sorted_headers;

  if(!list){
    return sorted_headers;
  }

  for( ; list; list = list->next){
    string strkey = list->data;
    size_t pos;
    if(string::npos != (pos = strkey.find(':', 0))){
      strkey = strkey.substr(0, pos);
    }
    if(0 < sorted_headers.length()){
      sorted_headers += ";";
    }
    sorted_headers += lower(strkey);
  }

  return sorted_headers;
}
#if 0
string get_canonical_headers(const struct curl_slist* list)
{
  string canonical_headers;

  if(!list){
    canonical_headers = "\n";
    return canonical_headers;
  }

  for( ; list; list = list->next){
    string strhead = list->data;
    size_t pos;
    if(string::npos != (pos = strhead.find(':', 0))){
      string strkey = trim(lower(strhead.substr(0, pos)));
      string strval = trim(strhead.substr(pos + 1));
      strhead       = strkey + string(":") + strval;
    }else{
      strhead       = trim(lower(strhead));
    }
    canonical_headers += strhead;
    canonical_headers += "\n";
  }
  return canonical_headers;
}
#endif

string get_canonical_headers(const struct curl_slist* list)
{
  string canonical_headers;

  if(!list){
    canonical_headers = "\n";
    return canonical_headers;
  }

  for( ; list; list = list->next){
    string strhead = list->data, strkey;
    size_t pos;
    if(string::npos != (pos = strhead.find(':', 0))){
      strkey = trim(lower(strhead.substr(0, pos)));
      string strval = trim(strhead.substr(pos + 1));
      if (strval.empty()) {
         continue;
      }
      //value needs to be urlEncode
      strhead = strkey + string("=") + urlEncodeForSign(strval);
    }else{
      strhead = trim(lower(strhead));
    }
    if (!is_signed_header(strkey)) {
        continue;
    }
    canonical_headers += strhead;
    canonical_headers += "&";
  }
  if (canonical_headers.size() > 0 && canonical_headers.at(canonical_headers.size() - 1) == '&') {
      canonical_headers[canonical_headers.size() - 1] = '\n';
  } else {
      canonical_headers += '\n';
  }
  return canonical_headers;
}

string get_canonical_header_keys(const struct curl_slist* list)
{
  string canonical_headers;

  if(!list){;
    return "";
  }

  for( ; list; list = list->next){
    string strhead = list->data;
    size_t pos;
    if(string::npos != (pos = strhead.find(':', 0))){
      string strkey = trim(lower(strhead.substr(0, pos)));
      string strval = trim(strhead.substr(pos + 1));
      if (strval.empty()) {
         continue;
      }
      strhead       = strkey;
    }else{
      strhead       = trim(lower(strhead));
    }
    if (!is_signed_header(strhead)) {
      continue;
    }
    canonical_headers += strhead;
    canonical_headers += ";";
  }
  if (canonical_headers.size() > 0 && canonical_headers.at(canonical_headers.size() - 1) == ';') {
      canonical_headers = canonical_headers.substr(0, canonical_headers.size() - 1);
  }

  return canonical_headers;
}


// function for using global values
bool MakeUrlResource(const char* realpath, string& resourcepath, string& url)
{
  if(!realpath){
    return false;
  }

  string resourcepathwithbucket;
  if (*realpath == '/')  {
      resourcepath = realpath;
  } else {
      resourcepath = service_path + realpath;
  }
  resourcepathwithbucket = service_path + bucket + realpath;
  url          = host + urlEncode(resourcepathwithbucket);
  return true;
}

string prepare_url(const char* url, string &host)
{
  S3FS_PRN_INFO3("URL is %s", url);

  string uri;
  string path;
  string url_str = str(url);
  string token =  str("/" + bucket);
  int bucket_pos = url_str.find(token);
  int bucket_length = token.size();
  int uri_length = 0;

  if(!strncasecmp(url_str.c_str(), "https://", 8)){
    uri_length = 8;
  } else if(!strncasecmp(url_str.c_str(), "http://", 7)) {
    uri_length = 7;
  }
  uri  = url_str.substr(0, uri_length);

  //if token is /cos and url_str starts with https://cos or http://cos, we need to find the second token.
  if("/cos" == token && (!strncasecmp(url_str.c_str(), "https://cos", 11) || 
                         !strncasecmp(url_str.c_str(), "http://cos", 10))) {
    bucket_pos = url_str.find(token, uri_length);
  }

  if(!pathrequeststyle){
    host = bucket + "-" +  appid + "." + url_str.substr(uri_length, bucket_pos - uri_length).c_str();
    path = url_str.substr((bucket_pos + bucket_length));
  }else{
    host = url_str.substr(uri_length, bucket_pos - uri_length).c_str();
    string part = url_str.substr((bucket_pos + bucket_length));
    if('/' != part[0]){
      part = "/" + part;
    }
    path = "/" + bucket + part;
  }

  url_str = uri + host + path;

  S3FS_PRN_INFO3("URL changed is %s", url_str.c_str());

  return str(url_str);
}

bool is_signed_header(const string &key) {
  if (key == "host" ||
    key == "content-length" ||
    key == "content-type" ||
    key == "content-md5" ||
    key == "range") {
    return true;
  }
  if (key.substr(0, 5) == "x-cos") {
    return true;
  }
  return false;
}

map<string, string> get_params_from_query_string(const string &query)
{
  map<string, string> params;

  stringstream ss(query);
  string subquery;
  while (getline(ss, subquery, '&')) {
    if (!subquery.empty()) {
      size_t pos;
      string key, val;
      if (string::npos != (pos = subquery.find('=', 0))) {
        key = subquery.substr(0, pos);
        val = subquery.substr(pos + 1);
      } else {
        key = subquery;
      }
      params[key] = val;
    }
  }
  return params;
}

string get_canonical_params(const map<string, string> &requestParams)
{
  string canonical_params;
  map<string, string>::const_iterator iter = requestParams.begin();
  for (; iter != requestParams.end(); iter++) {
    // value has been encoded
    canonical_params += lower(urlEncodeForSign(iter->first)) + string("=") + urlEncodeForSign(iter->second);
    canonical_params += "&";
  }
  if (canonical_params.size() > 0 && canonical_params.at(canonical_params.size() - 1) == '&') {
      canonical_params[canonical_params.size() - 1] = '\n';
  } else {
      canonical_params += '\n';
  }
  return canonical_params;
}

string get_canonical_param_keys(const map<string, string> &requestParams)
{
  string canonical_params;
  map<string, string>::const_iterator iter = requestParams.begin();
  for (; iter != requestParams.end(); iter++) {
    canonical_params += lower(urlEncodeForSign(iter->first));
    canonical_params += ";";
  }
  if (canonical_params.size() > 0 && canonical_params.at(canonical_params.size() - 1) == ';') {
    canonical_params = canonical_params.substr(0, canonical_params.size() - 1);
  }
  return canonical_params;
}

/*
* Local variables:
* tab-width: 4
* c-basic-offset: 4
* End:
* vim600: noet sw=4 ts=4 fdm=marker
* vim<600: noet sw=4 ts=4
*/
