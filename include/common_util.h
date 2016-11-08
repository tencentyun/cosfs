#ifndef COMMON_UTIL_H
#define COMMON_UTIL_H
#include <string>
#include <stdint.h>
#include "string_util.h"
#include "cos-cpp-sdk/CosApi.h"
#include "cos-cpp-sdk/Auth.h"

using std::string;

class CommonUtil {
public:
    static void s3fs_usr2_handler(int sig);
    static bool set_s3fs_usr2_handler(void);
    static void show_version(void);
    static void show_help(void);
    static bool parseConfig(const char* config_file);
    static string getCosHostHeader();
    static string getCosDomain();
    static string GetDownLoadSign();
    static bool checkCosInfoValid();
};
#endif
