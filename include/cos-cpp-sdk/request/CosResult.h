#ifndef COS_RESULT_H
#define COS_RESULT_H
#include <string>

using std::string;
namespace qcloud_cos{

class CosResult
{
public:
    CosResult();
    CosResult(int code, string message);
    ~CosResult();
    void setCode(int code);
    void setMessage(string message);
    string toJsonString();
private:
    int code;
    string message;
};

}


#endif
