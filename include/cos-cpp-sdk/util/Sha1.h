#ifndef SHA_H
#define SHA_H
//#ifdef __cplusplus
//extern "C" {
//#endif
#include <stdlib.h>
#include <stdio.h>
#include <string>

using namespace std;

namespace qcloud_cos{

/* Useful defines & typedefs */
typedef unsigned char SHA_BYTE;	/* 8-bit quantity */
typedef unsigned int  SHA_LONG;	/* 32-or-more-bit quantity */
#define SHA_BYTE_ORDER 1234
#define SHA_VERSION 1

#define SHA_BLOCKSIZE		64
#define SHA_DIGESTSIZE		20

typedef struct {
    SHA_LONG digest[5];		/* message digest */
    SHA_LONG count_lo, count_hi;	/* 64-bit bit count */
    SHA_BYTE data[SHA_BLOCKSIZE];	/* SHA data buffer */
    int local;			/* unprocessed amount in data */
} SHA_INFO;


void sha_init(SHA_INFO *);
void sha_update(SHA_INFO *, SHA_BYTE *, int);
void sha_final(unsigned char [20], SHA_INFO *);

void sha_stream(unsigned char [20], SHA_INFO *, FILE *, int);
void sha_print(unsigned char [20]);
void sha_output(unsigned char [20],unsigned char [40]);
const char *sha_version(void);

class Sha1
{
public:
    Sha1();
    ~Sha1();

    void append(const char* data, unsigned int size);
    string hexdigest();
    string final();

private:
    SHA_INFO    m_sha;   
};

}

//#ifdef __cplusplus
//}
//#endif

#endif /* SHA_H */


