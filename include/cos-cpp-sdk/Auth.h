#ifndef AUTH_H
#define AUTH_H

#include <stdint.h>
#include <string>

using std::string;
namespace qcloud_cos {

class Auth {
public:
    /// @brief 返回多次有效的签名，可以在有效期内重复对多个资源使用，以下参数含义均可在qcloud官网上
    ///        获取帮助：https://www.qcloud.com/doc/product/227/%E5%9F%BA%E6%9C%AC%E6%A6%82%E5%BF%B5
    /// @param app_id 项目的app_id
    /// @param secret_id 签名秘钥id，可在控制台获得
    /// @param secret_key 签名秘钥，可在控制台获得
    /// @param expired_time_in_s 过期时间，从当前开始的过期时间，单位秒
    /// @param bucket_name 文件所在的bucket
    /// @return string 签名，可用于访问文件，返回空串代表失败
    static std::string AppSignMuti(uint64_t app_id,
                                   const std::string& secret_id,
                                   const std::string& secret_key,
                                   uint64_t expired_time_in_s,
                                   const std::string& bucket_name);

    /// @brief 返回单次有效的签名，用于删除和更新指定path的资源，仅可使用一次
    ///        获取帮助：https://www.qcloud.com/doc/product/227/%E5%9F%BA%E6%9C%AC%E6%A6%82%E5%BF%B5
    /// @param app_id 项目的app_id
    /// @param secret_id 签名秘钥id，可在控制台获得
    /// @param secret_key 签名秘钥，可在控制台获得
    /// @param path 文件在bucket中的位置
    /// @param bucket_name 文件所在的bucket，返回空串代表失败
    /// @return string 签名，可用于访问文件
    static std::string AppSignOnce(uint64_t app_id,
                                   const std::string& secret_id,
                                   const std::string& secret_key,
                                   const std::string& path,
                                   const std::string& bucket_name);


private:
    static std::string AppSignInternal(uint64_t app_id,
                                       const std::string& secret_id,
                                       const std::string& secret_key,
                                       uint64_t expired_time_in_s,
                                       const std::string& file_id,
                                       const std::string& bucket_name);

private:
    // 禁止该类各种构造
    Auth() {}
    ~Auth() {}

    Auth(const Auth&);
    Auth& operator=(const Auth&);
};

} // namespace qcloud_cos

#endif // AUTH_H
