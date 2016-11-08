运行环境: Linux
依赖动态库：dl z ssl crypto stdc++ pthread
静态库(程序包已携带): cossdk fuse curl jsoncpp

1、编译
cmake .
make

2、配置文件cos_config.json(主要是cosfs依赖的COS SDK使用)
参数说明:
"AppID":1251668577,
"SecretID":"AKIDWtTCBYjM5OwLB9CAwA1Qb2ThTSUjfGFO",
"SecretKey":"FZjRSu0mJ9YJijVXXY57MAdCl4uylaA7",
"Region":"sh",                    //所属COS区域，上传下载操作的URL均与该参数有关
"SignExpiredTime":360,            //签名超时时间，单位：秒
"CurlConnectTimeoutInms":10000,   //CURL连接超时时间，单位：毫秒
"CurlGlobalConnectTimeoutInms":360000, //CURL连接执行最大时间，单位：毫秒
"UploadSliceSize":1048576,        //分片大小，单位：字节，可选的有512k,1M,2M,3M(需要换算成对应字节数)
"IsUploadTakeSha":0,              //上传文件时是否需要携带sha值
"DownloadDomainType":2,           //下载域名类型：1: cdn, 2: cos, 3: innercos, 4: self domain
"SelfDomain":"",                  //自定义域名
"UploadThreadPoolSize":5          //单文件分片上传线程池大小
"AsynThreadPoolSize":2            //异步上传下载线程池大小
"LogoutType":0                    //打印输出，0:不输出,1:输出到屏幕,2:打印syslog
一般只需要修改如下COS信息参数：
AppID、SecretID、SecretKey、Region、DownloadDomainType
3、运行
(1)挂载bucket到本地/mnt/mointpoint/
./cosfs bucket /mnt/mointpoint/ -o cfgfile=cos_config.json

(2)挂载bucket下的某个目录到本地/mnt/mointpoint/
./cosfs bucket:/folder /mnt/mointpoint/ -o cfgfile=cos_config.json

4、卸载
先要安装fuse(yum install fuse),才能执行
fusermount -u /mnt/mointpoint/
如果提示设备忙,则需退出/mnt/mointpoint/,如果还是无法卸载,则执行
umount -l /mnt/mointpoint/

备注：
目前支持的操作(只读相关操作,写操作暂时不支持)：
linux命令：cd, ls, ll, cat, more, cp
系统接口:  open(), read(), stat(), close(), lseek()
