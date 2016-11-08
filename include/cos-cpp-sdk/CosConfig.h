#ifndef COS_CONFIG_H
#define COS_CONFIG_H
#include <string>
#include <stdint.h>

using std::string;
namespace qcloud_cos{

class CosConfig{
 
public:
    CosConfig(const CosConfig& config) {
        this->appid = config.appid;
        this->secret_id = config.secret_id;
        this->secret_key = config.secret_key;
    }
    CosConfig(const string& config_file);
    uint64_t getAppid() const;
    string getSecretId() const;
    string getSecretKey() const;
    CosConfig(){}
    CosConfig(uint64_t t_appid,const std::string& t_secret_id,
                       const std::string& t_secret_key)
        : appid(t_appid), secret_id(t_secret_id), secret_key(t_secret_key){}
private:
    uint64_t appid;
    string secret_id;
    string secret_key;
};


}

#endif
