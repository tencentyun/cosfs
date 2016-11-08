#ifndef FILE_UTIL_H
#define FILE_UTIL_H
#include <stdint.h>
#include <iostream>
#include <fstream>
#include <string>

using std::string;
namespace qcloud_cos{

class FileUtil
{
    public:
    //获取文件内容
    static string getFileContent(const std::string& path);

    //返回文件大小
    static uint64_t getFileLen(const std::string& path);

    //判断文件是否存在
    static bool isFileExists(const std::string& path);
		
    /**
     * @brief 格式化路径，去除重复的分隔符,自动补齐开始的分隔符
     *
     * @param path  待格式化的路径字符串
     *
     * @return 格式化后的路径字符串
     */
    static std::string FormatPath(const std::string& path);

    /**
     * @brief 对path格式化目录路径，即自动帮用户添加路径结尾的'/', 针对查询
     *
     * @param path 待格式化的目录路径
     *
     * @return 格式化的目录路径
     */
    static std::string FormatFolderPath(const std::string& path);

    /**
     * @brief 对path格式化文件路径，即自动帮助用户去除路径结尾的'/', 针对查询
     *
     * @param path 待格式化的文件路径
     *
     * @return 格式化的文件路径
     */
    static std::string FormatFilePath(const std::string& path);

    /**
     * @brief  判断是否是合法的cos文件路径,即非空，非根路径，以'/'开始, 非'/'结尾, 针对上传与更新
     *
     * @param path  cos的文件路径
     *
     * @return      合法返回true, 否则false
     */
    static bool IsLegalFilePath(const std::string& path);

    /**
     * @brief  判断是否是合法的cos目录路径,即非空，非根路径，以'/'开始, '/'结尾, 针对上传与更新
     *
     * @param path  cos的目录路径
     *
     * @return      合法返回true, 否则false
     */
    static bool isLegalFolderPath(const std::string& path);

    /**
     * @brief 判断路径是否是根路径
     *
     * @param dstPath cos的远程路径
     *
     * @return 如果是返回true, 否则false
     */
    static bool IsRootPath(const std::string& dstPath);

    /**
     * @brief 生成URL编码后的远程路径
     *
     * @param dstPath 远程路径，即bucket下的路径
     *
     * @return 返回URL编码后的远程路径
     */
    static std::string EncodePath(const std::string& dstPath);

    /**
     * @brief 返回完整的URL编码后的Cos路径,包括CGI,APPID,bucketName以及远程路径
     *
     * @param endpoint  访问的地址
     * @param bucketName  buket名称
     * @param dstPath   远程路径
     * @param appid      appid
     *
     * @return 完整的URL编码后的cos路径
     */
    static std::string GetEncodedCosUrl(const std::string& endpoint,
                                        const std::string& bucketName,
                                        const std::string& dstPath, int appid);

    /**
     * @brief  生成签名失效的时间, 针对非一次性签名
     *
     * @return 返回对应的UNIX时间(秒)
     */
    static uint64_t GetExpiredTime();

    /**
     * @brief  文件夹名字是否合法
     *
     * @return 
     *
     *  文件夹名字中不能包含:'/' , '?' , '*' , ':' , '|' , '\' , '<' , '>' , '"'
     */
    static bool isValidFolderName(const string& foldername);

};

}

#endif
