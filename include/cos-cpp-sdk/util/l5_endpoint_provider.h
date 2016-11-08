// Copyright (c) 2016, Tencent Inc.
// All rights reserved.
//
// Author: rabbitliu <rabbitliu@tencent.com>
// Created: 06/06/16
// Description:

#ifndef L5_ENDPOINT_PROVIDER_H
#define L5_ENDPOINT_PROVIDER_H
#pragma once

#include <stdint.h>
#include <string>

namespace qcloud_cos {
class L5EndpointProvider {
public:
    L5EndpointProvider() {}
    ~L5EndpointProvider() {}

    static bool GetEndPoint(int64_t modid, int64_t cmdid, std::string* endpoint);
    // 根据json串上报数据
    static bool UpdateRouterResult(const std::string& full_path,
                                   int64_t modid, int64_t cmdid,
                                   int64_t use_time, int ret);

private:
    static bool ParseEndpoint(const std::string& full_path,
                              std::string* host, int* port);
};
} // namespace qcloud_cos

#endif // L5_ENDPOINT_PROVIDER_H
