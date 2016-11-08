/*************************************************************************
  Copyright (C) 2016 Tencent Inc.
  All rights reserved.

  > File Name: true_random.h
  > Author: chengwu
  > Mail: chengwu@tencent.com
  > Created Time: Thu 28 Apr 2016 11:14:00 AM CST
  > Description: 使用/dev/urandom生成真随机数
 ************************************************************************/

#ifndef _TRUE_RANDOM_H
#define _TRUE_RANDOM_H
#pragma once

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>

namespace qcloud_cos {
/// true random generator
class TrueRandom {
public:
    TrueRandom();
    ~TrueRandom();

    /// return random integer in range [0, UINT64_MAX]
    uint64_t NextUInt64();

    /// generate random bytes
    bool NextBytes(void* buffer, size_t size);

private:
    int m_fd;               /// fd for /dev/urandom
};
} // namespace Qcloud_cos
#endif // TRUE_RANDOM_H
