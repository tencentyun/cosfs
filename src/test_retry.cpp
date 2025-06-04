#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <curl/curl.h>
#include <string>
#include <map>
#include <list>
#include <vector>

#include "common.h"
#include "curl.h"
#include "s3fs.h"
#include "string_util.h"
#include "s3fs.h"
#include "s3fs_util.h"
#include "s3fs_auth.h"
#include "fdcache.h"
#include "test_util.h"

extern std::string host;
extern std::string bucket;
extern std::string appid;

void init()
{
    if(!S3fsCurl::InitS3fsCurl("/etc/mime.types")){
        exit(EXIT_FAILURE);
    }
    S3fsCurl::SetReadwriteTimeout(1);
    host = "http://cos.ap-chengdu.myqcloud.com";
    bucket = "cos-sdk-err-retry";
    appid  = "1253960454";
    S3fsCurl::SetPublicBucket(true);
}

void test_get_retry()
{
    init();

    FILE *file = tmpfile();
    ASSERT_NONIL(file);

    int fd = fileno(file);
    ASSERT_NOTEQUALS(fd, -1);

    int ret;
    {
        S3fsCurl curl;
        ret = curl.GetObjectRequest("/200r", fd, 0, 4096);
        ASSERT_EQUALS(ret, 0);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 1);
    }
    {
        S3fsCurl curl;
        ret = curl.GetObjectRequest("/204r", fd, 0, 4096);
        ASSERT_EQUALS(ret, 0);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 1);
    }
    {
        S3fsCurl curl;
        ret = curl.GetObjectRequest("/206r", fd, 0, 4096);
        ASSERT_EQUALS(ret, 0);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 1);
    }
    {
        S3fsCurl curl;
        ret = curl.GetObjectRequest("/400r", fd, 0, 4096);
        ASSERT_EQUALS(ret, -EIO);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 1);
    }
    {
        S3fsCurl curl;
        ret = curl.GetObjectRequest("/403r", fd, 0, 4096);
        ASSERT_EQUALS(ret, -EPERM);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 1);
    }
    {
        S3fsCurl curl;
        ret = curl.GetObjectRequest("/404r", fd, 0, 4096);
        ASSERT_EQUALS(ret, -ENOENT);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 1);
    }
    {
        S3fsCurl curl;
        ret = curl.GetObjectRequest("/500r", fd, 0, 4096);
        ASSERT_EQUALS(ret, -EIO);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 3);
    }
    {
        S3fsCurl curl;
        ret = curl.GetObjectRequest("/503r", fd, 0, 4096);
        ASSERT_EQUALS(ret, -EIO);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 3);
    }
    {
        S3fsCurl curl;
        ret = curl.GetObjectRequest("/504r", fd, 0, 4096);
        ASSERT_EQUALS(ret, -EIO);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 3);
    }
    {
        S3fsCurl curl;
        ret = curl.GetObjectRequest("/shutdown", fd, 0, 4096);
        ASSERT_EQUALS(ret, -EIO);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 3);
    }
    {
        S3fsCurl curl;
        ret = curl.GetObjectRequest("/timeout", fd, 0, 4096);
        ASSERT_EQUALS(ret, -EIO);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 3);
    }
    fclose(file);
}

void test_put_retry()
{
    init();

    headers_t meta;
    int ret;
    {
        S3fsCurl curl;
        ret = curl.PutRequest("/200r", meta, -1);
        ASSERT_EQUALS(ret, 0);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 1);
    }
    {
        S3fsCurl curl;
        ret = curl.PutRequest("/204r", meta, -1);
        ASSERT_EQUALS(ret, 0);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 1);
    }
    {
        S3fsCurl curl;
        ret = curl.PutRequest("/206r", meta, -1);
        ASSERT_EQUALS(ret, 0);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 1);
    }
    {
        S3fsCurl curl;
        ret = curl.PutRequest("/400r", meta, -1);
        ASSERT_EQUALS(ret, -EIO);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 1);
    }
    {
        S3fsCurl curl;
        ret = curl.PutRequest("/403r", meta, -1);
        ASSERT_EQUALS(ret, -EPERM);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 1);
    }
    {
        S3fsCurl curl;
        ret = curl.PutRequest("/404r", meta, -1);
        ASSERT_EQUALS(ret, -ENOENT);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 1);
    }
    {
        S3fsCurl curl;
        ret = curl.PutRequest("/500r", meta, -1);
        ASSERT_EQUALS(ret, -EIO);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 3);
    }
    {
        S3fsCurl curl;
        ret = curl.PutRequest("/503r", meta, -1);
        ASSERT_EQUALS(ret, -EIO);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 3);
    }
    {
        S3fsCurl curl;
        ret = curl.PutRequest("/504r", meta, -1);
        ASSERT_EQUALS(ret, -EIO);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 3);
    }
    {
        S3fsCurl curl;
        ret = curl.PutRequest("/shutdown", meta, -1);
        ASSERT_EQUALS(ret, -EIO);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 3);
    }
    {
        S3fsCurl curl;
        ret = curl.PutRequest("/timeout", meta, -1);
        ASSERT_EQUALS(ret, -EIO);
        ASSERT_EQUALS(curl.GetTestRequestCount(), 3);
    }
}
