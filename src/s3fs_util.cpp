/*
 * cosfs - FUSE-based file system backed by Tencentyun COS
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
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <syslog.h>
#include <pthread.h>
#include <sys/types.h>
#include <dirent.h>

#include <string>
#include <sstream>
#include <map>
#include <list>

#include "common.h"
#include "s3fs_util.h"
#include "string_util.h"
#include "s3fs.h"
#include "s3fs_auth.h"

using namespace std;

//-------------------------------------------------------------------
// Global valiables
//-------------------------------------------------------------------
std::string mount_prefix   = "";
mode_t gDefaultPermission = 0777;

//-------------------------------------------------------------------
// Utility
//-------------------------------------------------------------------
string get_realpath(const char *path) {
  string realpath = mount_prefix;
  realpath += path;

  return realpath;
}

//-------------------------------------------------------------------
// Class S3ObjList
//-------------------------------------------------------------------
// New class S3ObjList is base on old cos_object struct.
// This class is for OSS compatible clients.
//
// If name is terminated by "/", it is forced dir type.
// If name is terminated by "_$folder$", it is forced dir type.
// If is_dir is true and name is not terminated by "/", the name is added "/".
//
bool S3ObjList::insert(const char* name, const char* etag, bool is_dir)
{
  if(!name || '\0' == name[0]){
    return false;
  }

  s3obj_t::iterator iter;
  string newname;
  string orgname = name;

  // Normalization
  string::size_type pos = orgname.find("_$folder$");
  if(string::npos != pos){
    newname = orgname.substr(0, pos);
    is_dir  = true;
  }else{
    newname = orgname;
  }
  if(is_dir){
    if('/' != newname[newname.length() - 1]){
      newname += "/";
    }
  }else{
    if('/' == newname[newname.length() - 1]){
      is_dir = true;
    }
  }

  // Check derived name object.
  if(is_dir){
    string chkname = newname.substr(0, newname.length() - 1);
    if(objects.end() != (iter = objects.find(chkname))){
      // found "dir" object --> remove it.
      objects.erase(iter);
    }
  }else{
    string chkname = newname + "/";
    if(objects.end() != (iter = objects.find(chkname))){
      // found "dir/" object --> not add new object.
      // and add normalization
      return insert_nomalized(orgname.c_str(), chkname.c_str(), true);
    }
  }

  // Add object
  if(objects.end() != (iter = objects.find(newname))){
    // Found same object --> update information.
    (*iter).second.normalname.erase();
    (*iter).second.orgname = orgname;
    (*iter).second.is_dir  = is_dir;
    if(etag){
      (*iter).second.etag = string(etag);  // over write
    }
  }else{
    // add new object
    s3obj_entry newobject;
    newobject.orgname = orgname;
    newobject.is_dir  = is_dir;
    if(etag){
      newobject.etag = etag;
    }
    objects[newname] = newobject;
  }

  // add normalization
  return insert_nomalized(orgname.c_str(), newname.c_str(), is_dir);
}

bool S3ObjList::insert_nomalized(const char* name, const char* normalized, bool is_dir)
{
  if(!name || '\0' == name[0] || !normalized || '\0' == normalized[0]){
    return false;
  }
  if(0 == strcmp(name, normalized)){
    return true;
  }

  s3obj_t::iterator iter;
  if(objects.end() != (iter = objects.find(name))){
    // found name --> over write
    (*iter).second.orgname.erase();
    (*iter).second.etag.erase();
    (*iter).second.normalname = normalized;
    (*iter).second.is_dir     = is_dir;
  }else{
    // not found --> add new object
    s3obj_entry newobject;
    newobject.normalname = normalized;
    newobject.is_dir     = is_dir;
    objects[name]        = newobject;
  }
  return true;
}

const s3obj_entry* S3ObjList::GetS3Obj(const char* name) const
{
  s3obj_t::const_iterator iter;

  if(!name || '\0' == name[0]){
    return NULL;
  }
  if(objects.end() == (iter = objects.find(name))){
    return NULL;
  }
  return &((*iter).second);
}

string S3ObjList::GetOrgName(const char* name) const
{
  const s3obj_entry* ps3obj;

  if(!name || '\0' == name[0]){
    return string("");
  }
  if(NULL == (ps3obj = GetS3Obj(name))){
    return string("");
  }
  return ps3obj->orgname;
}

string S3ObjList::GetNormalizedName(const char* name) const
{
  const s3obj_entry* ps3obj;

  if(!name || '\0' == name[0]){
    return string("");
  }
  if(NULL == (ps3obj = GetS3Obj(name))){
    return string("");
  }
  if(0 == (ps3obj->normalname).length()){
    return string(name);
  }
  return ps3obj->normalname;
}

string S3ObjList::GetETag(const char* name) const
{
  const s3obj_entry* ps3obj;

  if(!name || '\0' == name[0]){
    return string("");
  }
  if(NULL == (ps3obj = GetS3Obj(name))){
    return string("");
  }
  return ps3obj->etag;
}

bool S3ObjList::IsDir(const char* name) const
{
  const s3obj_entry* ps3obj;

  if(NULL == (ps3obj = GetS3Obj(name))){
    return false;
  }
  return ps3obj->is_dir;
}

bool S3ObjList::GetLastName(std::string& lastname) const
{
  bool result = false;
  lastname = "";
  for(s3obj_t::const_iterator iter = objects.begin(); iter != objects.end(); ++iter){
    if((*iter).second.orgname.length()){
      if(0 > strcmp(lastname.c_str(), (*iter).second.orgname.c_str())){
        lastname = (*iter).second.orgname;
        result = true;
      }
    }else{
      if(0 > strcmp(lastname.c_str(), (*iter).second.normalname.c_str())){
        lastname = (*iter).second.normalname;
        result = true;
      }
    }
  }
  return result;
}

bool S3ObjList::GetNameList(s3obj_list_t& list, bool OnlyNormalized, bool CutSlash) const
{
  s3obj_t::const_iterator iter;

  for(iter = objects.begin(); objects.end() != iter; ++iter){
    if(OnlyNormalized && 0 != (*iter).second.normalname.length()){
      continue;
    }
    string name = (*iter).first;
    if(CutSlash && 1 < name.length() && '/' == name[name.length() - 1]){
      // only "/" string is skio this.
      name = name.substr(0, name.length() - 1);
    }
    list.push_back(name);
  }
  return true;
}

typedef std::map<std::string, bool> s3obj_h_t;

bool S3ObjList::MakeHierarchizedList(s3obj_list_t& list, bool haveSlash)
{
  s3obj_h_t h_map;
  s3obj_h_t::iterator hiter;
  s3obj_list_t::const_iterator liter;

  for(liter = list.begin(); list.end() != liter; ++liter){
    string strtmp = (*liter);
    if(1 < strtmp.length() && '/' == strtmp[strtmp.length() - 1]){
      strtmp = strtmp.substr(0, strtmp.length() - 1);
    }
    h_map[strtmp] = true;

    // check hierarchized directory
    for(string::size_type pos = strtmp.find_last_of("/"); string::npos != pos; pos = strtmp.find_last_of("/")){
      strtmp = strtmp.substr(0, pos);
      if(0 == strtmp.length() || "/" == strtmp){
        break;
      }
      if(h_map.end() == h_map.find(strtmp)){
        // not found
        h_map[strtmp] = false;
      }
    }
  }

  // check map and add lost hierarchized directory.
  for(hiter = h_map.begin(); hiter != h_map.end(); ++hiter){
    if(false == (*hiter).second){
      // add hierarchized directory.
      string strtmp = (*hiter).first;
      if(haveSlash){
        strtmp += "/";
      }
      list.push_back(strtmp);
    }
  }
  return true;
}

//-------------------------------------------------------------------
// Utility functions for moving objects
//-------------------------------------------------------------------
MVNODE *create_mvnode(const char *old_path, const char *new_path, bool is_dir, bool normdir)
{
  MVNODE *p;
  char *p_old_path;
  char *p_new_path;

  p = (MVNODE *) malloc(sizeof(MVNODE));
  if (p == NULL) {
    printf("create_mvnode: could not allocation memory for p\n");
    S3FS_FUSE_EXIT();
    return NULL;
  }

  if(NULL == (p_old_path = strdup(old_path))){
    free(p);
    printf("create_mvnode: could not allocation memory for p_old_path\n");
    S3FS_FUSE_EXIT();
    return NULL;
  }

  if(NULL == (p_new_path = strdup(new_path))){
    free(p);
    free(p_old_path);
    printf("create_mvnode: could not allocation memory for p_new_path\n");
    S3FS_FUSE_EXIT();
    return NULL;
  }

  p->old_path   = p_old_path;
  p->new_path   = p_new_path;
  p->is_dir     = is_dir;
  p->is_normdir = normdir;
  p->prev = NULL;
  p->next = NULL;
  return p;
}

//
// Add sorted MVNODE data(Ascending order)
//
MVNODE *add_mvnode(MVNODE** head, MVNODE** tail, const char *old_path, const char *new_path, bool is_dir, bool normdir)
{
  if(!head || !tail){
    return NULL;
  }

  MVNODE* cur;
  MVNODE* mvnew;
  for(cur = *head; cur; cur = cur->next){
    if(cur->is_dir == is_dir){
      int nResult = strcmp(cur->old_path, old_path);
      if(0 == nResult){
        // Found same old_path.
        return cur;

      }else if(0 > nResult){
        // next check.
        // ex: cur("abc"), mvnew("abcd")
        // ex: cur("abc"), mvnew("abd")
        continue;

      }else{
        // Add into before cur-pos.
        // ex: cur("abc"), mvnew("ab")
        // ex: cur("abc"), mvnew("abb")
        if(NULL == (mvnew = create_mvnode(old_path, new_path, is_dir, normdir))){
          return NULL;
        }
        if(cur->prev){
          (cur->prev)->next = mvnew;
        }else{
          *head = mvnew;
        }
        mvnew->prev = cur->prev;
        mvnew->next = cur;
        cur->prev = mvnew;

        return mvnew;
      }
    }
  }
  // Add into tail.
  if(NULL == (mvnew = create_mvnode(old_path, new_path, is_dir, normdir))){
    return NULL;
  }
  mvnew->prev = (*tail);
  if(*tail){
    (*tail)->next = mvnew;
  }
  (*tail) = mvnew;
  if(!(*head)){
    (*head) = mvnew;
  }
  return mvnew;
}

void free_mvnodes(MVNODE *head)
{
  MVNODE *my_head;
  MVNODE *next;

  for(my_head = head, next = NULL; my_head; my_head = next){
    next = my_head->next;
    free(my_head->old_path);
    free(my_head->new_path);
    free(my_head);
  }
  return;
}

//-------------------------------------------------------------------
// Class AutoLock
//-------------------------------------------------------------------
AutoLock::AutoLock(pthread_mutex_t* pmutex, Type type) : auto_mutex(pmutex)
{
    if (type == ALREADY_LOCKED) {
        is_lock_acquired = false;
    } else if (type == NO_WAIT) {
        int res = pthread_mutex_trylock(auto_mutex);
        if(res == 0){
            is_lock_acquired = true;
        }else if(res == EBUSY){
            is_lock_acquired = false;
        }else{
            S3FS_PRN_CRIT("pthread_mutex_trylock returned: %d", res);
            abort();
        }
    } else {
        int res = pthread_mutex_lock(auto_mutex);
        if(res == 0){
            is_lock_acquired = true;
        }else{
            S3FS_PRN_CRIT("pthread_mutex_lock returned: %d", res);
            abort();
        }
    }
}

AutoLock::~AutoLock()
{
    if (is_lock_acquired) {
        int res = pthread_mutex_unlock(auto_mutex);
        if(res != 0){
            S3FS_PRN_CRIT("pthread_mutex_lock returned: %d", res);
            abort();
        }
    }
}

//-------------------------------------------------------------------
// Utility for UID/GID
//-------------------------------------------------------------------
// get user name from uid
string get_username(uid_t uid)
{
  static size_t maxlen = 0;	// set onece
  char* pbuf;
  struct passwd pwinfo;
  struct passwd* ppwinfo = NULL;

  // make buffer
  if(0 == maxlen){
    long res = sysconf(_SC_GETPW_R_SIZE_MAX);
    if(0 > res){
      S3FS_PRN_WARN("could not get max pw length.");
      maxlen = 0;
      return string("");
    }
    maxlen = res;
  }
  if(NULL == (pbuf = (char*)malloc(sizeof(char) * maxlen))){
    S3FS_PRN_CRIT("failed to allocate memory.");
    return string("");
  }
  // get group information
  if(0 != getpwuid_r(uid, &pwinfo, pbuf, maxlen, &ppwinfo)){
    S3FS_PRN_WARN("could not get pw information.");
    free(pbuf);
    return string("");
  }
  // check pw
  if(NULL == ppwinfo){
    free(pbuf);
    return string("");
  }
  string name = SAFESTRPTR(ppwinfo->pw_name);
  free(pbuf);
  return name;
}

int is_uid_inculde_group(uid_t uid, gid_t gid)
{
  static size_t maxlen = 0;	// set onece
  int result;
  char* pbuf;
  struct group ginfo;
  struct group* pginfo = NULL;

  // make buffer
  if(0 == maxlen){
    long res = sysconf(_SC_GETGR_R_SIZE_MAX);
    if(0 > res){
      S3FS_PRN_ERR("could not get max name length.");
      maxlen = 0;
      return -ERANGE;
    }
    maxlen = res;
  }
  if(NULL == (pbuf = (char*)malloc(sizeof(char) * maxlen))){
    S3FS_PRN_CRIT("failed to allocate memory.");
    return -ENOMEM;
  }
  // get group information
  if(0 != (result = getgrgid_r(gid, &ginfo, pbuf, maxlen, &pginfo))){
    S3FS_PRN_ERR("could not get group information.");
    free(pbuf);
    return -result;
  }

  // check group
  if(NULL == pginfo){
    // there is not gid in group.
    free(pbuf);
    return -EINVAL;
  }

  string username = get_username(uid);

  char** ppgr_mem;
  for(ppgr_mem = pginfo->gr_mem; ppgr_mem && *ppgr_mem; ppgr_mem++){
    if(username == *ppgr_mem){
      // Found username in group.
      free(pbuf);
      return 1;
    }
  }
  free(pbuf);
  return 0;
}

//-------------------------------------------------------------------
// Utility for file and directory
//-------------------------------------------------------------------
// safe variant of dirname
// dirname clobbers path so let it operate on a tmp copy
string mydirname(string path)
{
  return string(dirname((char*)path.c_str()));
}

// safe variant of basename
// basename clobbers path so let it operate on a tmp copy
string mybasename(string path)
{
  return string(basename((char*)path.c_str()));
}

// mkdir --parents
int mkdirp(const string& path, mode_t mode)
{
  string       base;
  string       component;
  stringstream ss(path);
  while (getline(ss, component, '/')) {
    base += "/" + component;

    struct stat st;
    if(0 == stat(base.c_str(), &st)){
      if(!S_ISDIR(st.st_mode)){
        return EPERM;
      }
    }else{
      if(0 != mkdir(base.c_str(), mode)){
        return errno;
     }
    }
  }
  return 0;
}

bool check_exist_dir_permission(const char* dirpath)
{
  if(!dirpath || '\0' == dirpath[0]){
    return false;
  }

  // exists
  struct stat st;
  if(0 != stat(dirpath, &st)){
    if(ENOENT == errno){
      // dir does not exitst
      return true;
    }
    if(EACCES == errno){
      // could not access directory
      return false;
    }
    // somthing error occured
    return false;
  }

  // check type
  if(!S_ISDIR(st.st_mode)){
    // path is not directory
    return false;
  }

  // check permission
  uid_t myuid = geteuid();
  if(myuid == st.st_uid){
    if(S_IRWXU != (st.st_mode & S_IRWXU)){
      return false;
    }
  }else{
    if(1 == is_uid_inculde_group(myuid, st.st_gid)){
      if(S_IRWXG != (st.st_mode & S_IRWXG)){
        return false;
      }
    }else{
      if(S_IRWXO != (st.st_mode & S_IRWXO)){
        return false;
      }
    }
  }
  return true;
}

bool delete_files_in_dir(const char* dir, bool is_remove_own)
{
  DIR*           dp;
  struct dirent* dent;

  if(NULL == (dp = opendir(dir))){
    S3FS_PRN_ERR("could not open dir(%s) - errno(%d)", dir, errno);
    return false;
  }

  for(dent = readdir(dp); dent; dent = readdir(dp)){
    if(0 == strcmp(dent->d_name, "..") || 0 == strcmp(dent->d_name, ".")){
      continue;
    }
    string   fullpath = dir;
    fullpath         += "/";
    fullpath         += dent->d_name;
    struct stat st;
    if(0 != lstat(fullpath.c_str(), &st)){
      S3FS_PRN_ERR("could not get stats of file(%s) - errno(%d)", fullpath.c_str(), errno);
      closedir(dp);
      return false;
    }
    if(S_ISDIR(st.st_mode)){
      // dir -> Reentrant
      if(!delete_files_in_dir(fullpath.c_str(), true)){
        S3FS_PRN_ERR("could not remove sub dir(%s) - errno(%d)", fullpath.c_str(), errno);
        closedir(dp);
        return false;
      }
    }else{
      if(0 != unlink(fullpath.c_str())){
        S3FS_PRN_ERR("could not remove file(%s) - errno(%d)", fullpath.c_str(), errno);
        closedir(dp);
        return false;
      }
    }
  }
  closedir(dp);

  if(is_remove_own && 0 != rmdir(dir)){
    S3FS_PRN_ERR("could not remove dir(%s) - errno(%d)", dir, errno);
    return false;
  }
  return true;
}

//-------------------------------------------------------------------
// Utility functions for convert
//-------------------------------------------------------------------
time_t get_mtime(const char *s)
{
  return static_cast<time_t>(s3fs_strtoofft(s));
}

time_t get_mtime(const headers_t& meta, bool overcheck)
{
  headers_t::const_iterator iter;
  if(meta.end() == (iter = meta.find("x-cos-meta-mtime"))){
    if(overcheck){
      return get_lastmodified(meta);
    }
    return 0;
  }
  return get_mtime((*iter).second.c_str());
}

off_t get_size(const char *s)
{
  return s3fs_strtoofft(s);
}

off_t get_size(headers_t& meta)
{
  headers_t::const_iterator iter;
  if(meta.end() == (iter = meta.find("Content-Length"))){
    return 0;
  }
  return get_size((*iter).second.c_str());
}

mode_t get_mode(const char *s)
{
  return static_cast<mode_t>(s3fs_strtoofft(s));
}

mode_t get_mode(headers_t& meta, const char* path, bool checkdir, bool forcedir)
{
  mode_t mode = gDefaultPermission;
  bool isS3sync = false;
  headers_t::const_iterator iter;

  if(meta.end() != (iter = meta.find("x-cos-meta-mode"))){
    mode = get_mode((*iter).second.c_str());
  }else{
    if(meta.end() != (iter = meta.find("x-cos-meta-permissions"))){ // for s3sync
      mode = get_mode((*iter).second.c_str());
      isS3sync = true;
    }
  }
  // Checking the bitmask, if the last 3 bits are all zero then process as a regular
  // file type (S_IFDIR or S_IFREG), otherwise return mode unmodified so that S_IFIFO,
  // S_IFSOCK, S_IFCHR, S_IFLNK and S_IFBLK devices can be processed properly by fuse.
  if(!(mode & S_IFMT)){
    if(!isS3sync){
      if(checkdir){
        if(forcedir){
          mode |= S_IFDIR;
        }else{
          if(meta.end() != (iter = meta.find("Content-Type"))){
            string strConType = (*iter).second;
            if(strConType == "application/x-directory"){
              mode |= S_IFDIR;
            }else if(path && 0 < strlen(path) && '/' == path[strlen(path) - 1]){
              mode |= S_IFDIR;
            }else{
              mode |= S_IFREG;
            }
          }else{
            mode |= S_IFREG;
          }
        }
      }
    }else{
      if(!checkdir){
        // cut dir/reg flag.
        mode &= ~S_IFDIR;
        mode &= ~S_IFREG;
      }
    }
  }
  return mode;
}

uid_t get_uid(const char *s)
{
  return static_cast<uid_t>(s3fs_strtoofft(s));
}

uid_t get_uid(headers_t& meta)
{
  headers_t::const_iterator iter;
  if(meta.end() == (iter = meta.find("x-cos-meta-uid"))){
    if(meta.end() == (iter = meta.find("x-cos-meta-owner"))){ // for s3sync
      return 0;
    }
  }
  return get_uid((*iter).second.c_str());
}

gid_t get_gid(const char *s)
{
  return static_cast<gid_t>(s3fs_strtoofft(s));
}

gid_t get_gid(headers_t& meta)
{
  headers_t::const_iterator iter;
  if(meta.end() == (iter = meta.find("x-cos-meta-gid"))){
    if(meta.end() == (iter = meta.find("x-cos-meta-group"))){ // for s3sync
      return 0;
    }
  }
  return get_gid((*iter).second.c_str());
}

blkcnt_t get_blocks(off_t size)
{
  return size / 512 + 1;
}

time_t cvtCAMExpireStringToTime(const char* s)
{
  struct tm tm;
  if(!s){
    return 0L;
  }
  memset(&tm, 0, sizeof(struct tm));
  strptime(s, "%Y-%m-%dT%H:%M:%S", &tm);
  return timegm(&tm); // GMT
}

time_t get_lastmodified(const char* s)
{
  struct tm tm;
  if(!s){
    return 0L;
  }
  memset(&tm, 0, sizeof(struct tm));
  strptime(s, "%a, %d %b %Y %H:%M:%S %Z", &tm);
  return timegm(&tm); // GMT
}
// [NOTE]
// If add_noexist is false and the key does not exist, it will not be added.
//
bool merge_headers(headers_t& base, const headers_t& additional, bool add_noexist)
{
  bool added = false;
  for(headers_t::const_iterator iter = additional.begin(); iter != additional.end(); ++iter){
    if(add_noexist || base.find(iter->first) != base.end()){
      base[iter->first] = iter->second;
      added             = true;
    }
  }
  return added;
}

time_t get_lastmodified(const headers_t& meta)
{
  headers_t::const_iterator iter;
  if(meta.end() == (iter = meta.find("Last-Modified"))){
    return 0;
  }
  return get_lastmodified((*iter).second.c_str());
}

//
// Returns it whether it is an object with need checking in detail.
// If this function returns true, the object is possible to be directory
// and is needed checking detail(searching sub object).
//
bool is_need_check_obj_detail(headers_t& meta)
{
  headers_t::const_iterator iter;

  // directory object is Content-Length as 0.
  if(0 != get_size(meta)){
    return false;
  }
  // if the object has x-cos-meta information, checking is no more.
  if(meta.end() != meta.find("x-cos-meta-mode")  ||
     meta.end() != meta.find("x-cos-meta-mtime") ||
     meta.end() != meta.find("x-cos-meta-uid")   ||
     meta.end() != meta.find("x-cos-meta-gid")   ||
     meta.end() != meta.find("x-cos-meta-owner") ||
     meta.end() != meta.find("x-cos-meta-group") ||
     meta.end() != meta.find("x-cos-meta-permissions") )
  {
    return false;
  }
  // if there is not Content-Type, or Content-Type is "x-directory",
  // checking is no more.
  if(meta.end() == (iter = meta.find("Content-Type"))){
    return false;
  }
  if("application/x-directory" == (*iter).second){
    return false;
  }
  return true;
}

//-------------------------------------------------------------------
// Help
//-------------------------------------------------------------------
void show_usage (void)
{
  printf("Usage: %s BUCKET:[PATH] MOUNTPOINT [OPTION]...\n",
    program_name.c_str());
}

void show_help (void)
{
  show_usage();
  printf(
    "\n"
    "Mount an Tencent COS bucket as a file system.\n"
    "\n"
    "   General forms for cosfs and FUSE/mount options:\n"
    "      -o opt[,opt...]\n"
    "      -o opt [-o opt] ...\n"
    "\n"
    "cosfs Options:\n"
    "\n"
    "   Most cosfs options are given in the form where \"opt\" is:\n"
    "\n"
    "             <option_name>=<option_value>\n"
    "\n"
    "   default_acl (default=\"public-read\")\n"
    "     - the default canned acl to apply to all written cos objects\n"
    "\n"
    "   retries (default=\"2\")\n"
    "      - number of times to retry a failed cos transaction\n"
    "\n"
    "   use_cache (default=\"\" which means disabled)\n"
    "      - local folder to use for local file cache\n"
    "\n"
    "   del_cache (delete local file cache)\n"
    "      - delete local file cache when cosfs starts and exits.\n"
    "\n"
    "   tmpdir (default=\"/tmp\")\n"
    "      - local folder for temporary files.\n"
    "\n"
    "   user_agent_suffix (http request user agent suffix)\n"
    "      - specify http request user agent.\n"
    "\n"
    "   storage_class (default=\"standard\")\n"
    "      - store object with specified storage class.  Possible values:\n"
    "        standard, standard_ia, and reduced_redundancy.\n"
    "\n"
    "   public_bucket (default=\"\" which means disabled)\n"
    "      - anonymously mount a public bucket when set to 1\n"
    "\n"
    "   passwd_file (default=\"\")\n"
    "      - specify which cosfs password file to use\n"
    "\n"
    "   cam_role (default=\"\")\n"
    "      - specify cam role to request\n"
    "\n"
    "   cam_role_endpoint (default=\"\")\n"
    "      - specify cam role endpoint to request cam and get credentials\n"
    "\n"
    "   connect_timeout (default=\"300\" seconds)\n"
    "      - time to wait for connection before giving up\n"
    "\n"
    "   readwrite_timeout (default=\"60\" seconds)\n"
    "      - time to wait between read/write activity before giving up\n"
    "\n"
    "   max_stat_cache_size (default=\"1000\" entries (about 4MB))\n"
    "      - maximum number of entries in the stat cache\n"
    "\n"
    "   stat_cache_expire (default is no expire)\n"
    "      - specify expire time(seconds) for entries in the stat cache.\n"
    "\n"
    "   enable_noobj_cache (default is disable)\n"
    "      - enable cache entries for the object which does not exist.\n"
    "      cosfs always has to check whether file(or sub directory) exists \n"
    "      under object(path) when cosfs does some command, since cosfs has \n"
    "      recognized a directory which does not exist and has files or \n"
    "      sub directories under itself. It increases ListBucket request \n"
    "      and makes performance bad.\n"
    "      You can specify this option for performance, cosfs memorizes \n"
    "      in stat cache that the object(file or directory) does not exist.\n"
    "\n"
    "   no_check_certificate\n"
    "      - server certificate won't be checked against the available \n"
	"      certificate authorities.\n"
    "\n"
    "   nodnscache (disable dns cache)\n"
    "      - cosfs is always using dns cache, this option make dns cache disable.\n"
    "\n"
    "   nosscache (disable ssl session cache)\n"
    "      - cosfs is always using ssl session cache, this option make ssl \n"
    "      session cache disable.\n"
    "\n"
    "   multireq_max (default=\"20\")\n"
    "      - maximum number of parallel request for listing objects.\n"
    "\n"
    "   parallel_count (default=\"5\")\n"
    "      - number of parallel request for uploading big objects.\n"
    "      cosfs uploads large object(over 20MB) by multipart post request, \n"
    "      and sends parallel requests.\n"
    "      This option limits parallel request count which cosfs requests \n"
    "      at once. It is necessary to set this value depending on a CPU \n"
    "      and a network band.\n"
    "\n"
    "   multipart_size (default=\"10\")\n"
    "      - part size, in MB, for each multipart request.\n"
    "\n"
    "   ensure_diskfree (default same multipart_size value)\n"
    "      - sets MB to ensure disk free space. cosfs makes file for\n"
    "        downloading, uploading and caching files. If the disk free\n"
    "        space is smaller than this value, cosfs do not use diskspace\n"
    "        as possible in exchange for the performance.\n"
    "\n"
    "   singlepart_copy_limit (default=\"5120\")\n"
    "      - maximum size, in MB, of a single-part copy before trying \n"
    "      multipart copy.\n"
    "\n"
    "   url (default=\"\")\n"
    "      - sets the url to use to access tencentyun cos\n"
    "\n"
    "   default_permission (default=777)\n"
    "      - when the file do not have permission meta, cosfs will use this \n"
	"      defalut value.\n"
    "\n"
    "   endpoint (default=\"\")\n"
    "      - sets the endpoint to use on signature version 4\n"
    "      If the cosfs could not connect to the region specified\n"
    "      by this option, cosfs could not run. But if you do not specify this\n"
    "      option, and if you can not connect with the default region, cosfs\n"
    "      will retry to automatically connect to the other region. So cosfs\n"
    "      can know the correct region name, because cosfs can find it in an\n"
    "      error from the COS server.\n"
    "\n"
    "   mp_umask (default is \"0000\")\n"
    "      - sets umask for the mount point directory.\n"
    "      If allow_other option is not set, cosfs allows access to the mount\n"
    "      point only to the owner. In the opposite case cosfs allows access\n"
    "      to all users as the default. But if you set the allow_other with\n"
    "      this option, you can control the permissions of the\n"
    "      mount point by this option like umask.\n"
    "\n"
    "   nomultipart (disable multipart uploads)\n"
    "\n"
    "   disable_content_md5 (md5 default is enabled)\n"
    "      - content md5 will ensure data integrity during upload with MD5 hash.\n"
    "\n"
    "\n"
    "   nocopyapi (for other incomplete compatibility object storage)\n"
    "        For a distributed object storage which is compatibility COS\n"
    "        API without PUT(copy api).\n"
    "        If you set this option, cosfs do not use PUT with \n"
    "        \"x-cos-copy-source\"(copy api). Because traffic is increased\n"
    "        2-3 times by this option, we do not recommend this.\n"
    "\n"
    "   norenameapi (for other incomplete compatibility object storage)\n"
    "        For a distributed object storage which is compatibility COS\n"
    "        API without PUT(copy api).\n"
    "        This option is a subset of nocopyapi option. The nocopyapi\n"
    "        option does not use copy-api for all command(ex. chmod, chown,\n"
    "        touch, mv, etc), but this option does not use copy-api for\n"
    "        only rename command(ex. mv). If this option is specified with\n"
    "        nocopyapi, then cosfs ignores it.\n"
    "\n"
    "   use_path_request_style (use legacy API calling style)\n"
    "        Enble compatibility with OSS-like APIs which do not support\n"
    "        the virtual-host request style, by using the older path request\n"
    "        style.\n"
    "\n"
    "   dbglevel (default=\"crit\")\n"
    "        Set the debug message level. set value as crit(critical), err\n"
    "        (error), warn(warning), info(information) to debug level.\n"
    "        default debug level is critical. If cosfs run with \"-d\" option,\n"
    "        the debug level is set information. When cosfs catch the signal\n"
    "        SIGUSR2, the debug level is bumpup.\n"
    "\n"
    "   max_prefetch_bytes (default=\"100*1024*1024, unit: bytes\")\n"
    "        Set the pretech bytes, when read data from cos, the fetch bytes will decide by\n"
    "        max(size, min(parrellel_count*multipart_size, max_prefetch_bytes))\n"
    "\n"
    "   curldbg - put curl debug message\n"
    "        Put the debug message from libcurl when this option is specified.\n"
    "\n"
    "FUSE/mount Options:\n"
    "\n"
    "   Most of the generic mount options described in 'man mount' are\n"
    "   supported (ro, rw, suid, nosuid, dev, nodev, exec, noexec, atime,\n"
    "   noatime, sync async, dirsync).  Filesystems are mounted with\n"
    "   '-onodev,nosuid' by default, which can only be overridden by a\n"
    "   privileged user.\n"
    "   \n"
    "   There are many FUSE specific mount options that can be specified.\n"
    "   e.g. allow_other  See the FUSE's README for the full set.\n"
    "\n"
    "Miscellaneous Options:\n"
    "\n"
    " -h, --help        Output this help.\n"
    "     --version     Output version info.\n"
    " -d  --debug       Turn on DEBUG messages to syslog. Specifying -d\n"
    "                   twice turns on FUSE debug messages to STDOUT.\n"
    " -f                FUSE foreground option - do not run as daemon.\n"
    " -s                FUSE singlethread option\n"
    "                   disable multi-threaded operation\n"
    "\n"
    "\n"
  );
  return;
}

void show_version(void)
{
  printf(
  "Tencentyun Object Storage Service File System V%s(commit:%s) with %s\n"
  "License GPL2: GNU GPL version 2 <http://gnu.org/licenses/gpl.html>\n"
  "This is free software: you are free to change and redistribute it.\n"
  "There is NO WARRANTY, to the extent permitted by law.\n",
  VERSION, COMMIT_HASH_VAL, s3fs_crypt_lib_name());
  return;
}

/*
* Local variables:
* tab-width: 4
* c-basic-offset: 4
* End:
* vim600: noet sw=4 ts=4 fdm=marker
* vim<600: noet sw=4 ts=4
*/
