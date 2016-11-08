/*
 * ossfs - FUSE-based file system backed by Tencent -COS
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
#include "cosfs_util.h"
#include "string_util.h"
#include "cos_log.h"
using namespace std;
//-------------------------------------------------------------------
// Global valiables
//-------------------------------------------------------------------
std::string mount_prefix   = "";

//-------------------------------------------------------------------
// Utility
//-------------------------------------------------------------------
string get_realpath(const char *path) {
  string realpath = mount_prefix;
  realpath += path;

  return realpath;
}


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
      S3FS_PRN_WARN("could not get max pw length. res=%ld", res);
      maxlen = 0;
      return string("");
    }
    maxlen = res;
  }
  if(NULL == (pbuf = (char*)malloc(sizeof(char) * maxlen))){
    return string("");
  }
  // get group information
  if(0 != getpwuid_r(uid, &pwinfo, pbuf, maxlen, &ppwinfo)){
    S3FS_PRN_WARN("could not get pw information,uid=%d",uid);
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

//-------------------------------------------------------------------
// Utility functions for moving objects
//-------------------------------------------------------------------

int is_uid_inculde_group(uid_t uid, gid_t gid)
{
  static size_t maxlen = 0;	// set onece
  int result;
  char* pbuf;
  struct group ginfo;
  struct group* pginfo = NULL;

  // make buffer
  if(0 == maxlen){
    long ret = sysconf(_SC_GETGR_R_SIZE_MAX);
    if(ret < 0){
      //DPRNNN("could not get max name length.");
      maxlen = 0;
      return -ERANGE;
    }
    maxlen = static_cast<size_t>(ret);
  }
  if(NULL == (pbuf = (char*)malloc(sizeof(char) * maxlen))){
    //DPRNCRIT("failed to allocate memory.");
    return -ENOMEM;
  }
  // get group infomation
  if(0 != (result = getgrgid_r(gid, &ginfo, pbuf, maxlen, &pginfo))){
    //DPRNNN("could not get group infomation.");
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
  string base;
  string component;
  stringstream ss(path);
  while (getline(ss, component, '/')) {
    base += "/" + component;
    mkdir(base.c_str(), mode);
  }
  return 0;
}

//-------------------------------------------------------------------
// Utility functions for convert
//-------------------------------------------------------------------
time_t get_mtime(const char *s)
{
	return static_cast<time_t>(StringUtils::Str2Int(s));
}

time_t get_mtime(headers_t& meta)
{
	return get_lastmodified(meta);
}

off_t get_size(const char *s)
{
	return StringUtils::StringToUint64(s);
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
	return static_cast<mode_t>(StringUtils::Str2Int(s));
}

mode_t get_mode(headers_t& meta, const char* path)
{
	mode_t mode = 0;
	headers_t::const_iterator iter;
	if(meta.end() != (iter = meta.find("Content-Type")) && iter->second == "file") {
		mode = S_IFREG | 0444;
	} else {
		mode = S_IFDIR | 0555;
	}
	return mode;
}

uid_t get_uid(const char *s)
{
	return static_cast<uid_t>(StringUtils::Str2Int(s));
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
	return static_cast<gid_t>(StringUtils::Str2Int(s));
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

time_t cvtIAMExpireStringToTime(const char* s)
{
  struct tm tm;
  if(!s){
    return 0L;
  }
  memset(&tm, 0, sizeof(struct tm));
  strptime(s, "%Y-%m-%dT%H:%M:%S", &tm);
  return mktime(&tm);      // GMT
}

time_t get_lastmodified(const char* s)
{
  struct tm tm;
  if(!s){
    return 0L;
  }
  memset(&tm, 0, sizeof(struct tm));
  strptime(s, "%a, %d %b %Y %H:%M:%S %Z", &tm);
  return mktime(&tm);      // GMT
}

time_t get_lastmodified(headers_t& meta)
{
  headers_t::const_iterator iter;
  if(meta.end() == (iter = meta.find("Last-Modified"))){
    return 0;
  }
  return StringUtils::StringToUint64((*iter).second.c_str());
  //return get_lastmodified((*iter).second.c_str());
}
