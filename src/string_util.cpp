#include "string_util.h"
#include <iostream>
#include <stdio.h>
#include <stdlib.h>

using std::cerr;

string& StringUtils::Trim(string &s) {
    if (s.empty()) {
        return s;
    }

    s.erase(0, s.find_first_not_of(" "));
    s.erase(s.find_last_not_of(" ") + 1);
    return s;
}

string& StringUtils::TrimTailChar(string &s, char c) {
    if (s.empty()) {
        return s;
    }

    s.erase(0, s.find_first_not_of(c));
    s.erase(s.find_last_not_of(c) + 1);
    return s;
}

string StringUtils::JsonToString(const Json::Value &jsonObject) {
    Json::FastWriter fast_writer;
    return fast_writer.write(jsonObject);
}

Json::Value StringUtils::StringToJson(const string &jsonStr) {
    Json::Reader reader;
    Json::Value jsonObject;
    if (!reader.parse(jsonStr, jsonObject)) {
        std::cerr << "parse string to json error, origin str:" << jsonStr << std::endl;
    }
    return jsonObject;
}

string StringUtils::Uint64ToString(uint64_t num) {
    char buf[65];
#if __WORDSIZE == 64
    snprintf(buf, sizeof(buf), "%lu", num);
#else
    snprintf(buf, sizeof(buf), "%llu", num);
#endif
    string str(buf);
    return str;
}

string StringUtils::IntToString(int num) {
    char buf[65];
    snprintf(buf, sizeof(buf), "%d", num);
    string str(buf);
    return str;
}

int StringUtils::Str2Int(const string & str) {
    return atoi(str.c_str()); 
}

int StringUtils::Str2Int(const char* str) {
    return atoi(str); 
}

long StringUtils::Str2Long(const char* str) {
    return atol(str); 
}

long StringUtils::Str2Long(const string & str) {
    return atol(str.c_str()); 
}

uint64_t StringUtils::StringToUint64(const std::string& s)
{
    std::stringstream a;
    a << s;
    uint64_t ret=0;
    a >> ret;
    return ret;
}

