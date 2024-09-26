# COSFS


### 简介

cosfs 能让您在Linux/Mac OS X 系统中把Tencent COS bucket 挂载到本地文件
系统中，您能够便捷的通过本地文件系统操作COS 上的对象，实现数据的共享。

### 功能

cosfs 基于s3fs 构建，具有s3fs 的全部功能。主要功能包括：

* 支持POSIX 文件系统的大部分功能，包括文件读写，目录，链接操作，权限，uid/gid
* 通过COS 的multipart 功能上传大文件。
* MD5 校验保证数据完整性。

### 安装
 
#### 预编译安装
COS为了方便用户安装，提供了相应操作系统的安装包
 
* ubuntu-16.04 ubuntu-14.04
* centos6.5/centos7.0
 
可以从[版本发布页面](https://github.com/tencentyun/cosfs/releases)来下载安装

centos6.5+ 安装方式
 
```
sudo yum localinstall release-cosfs-package
其中 ， release-cosfs-package 需要改成用户系统对应的安装包的名称，如 release-cosfs-1.0.1-centos6.5.x86_64.rpm
```

ubuntu 安装方式

```
sudo apt-get update; sudo apt-get install gdebi-core
sudo  gdebi release-cosfs-package
```

#### 源码安装

如果没有找到对应的安装包，您也可以自行编译安装。编译前请先安装下列依赖库：

Ubuntu 14.04+:

```
sudo apt-get install automake autotools-dev g++ git libcurl4-gnutls-dev \
                     libfuse-dev libssl-dev libxml2-dev make pkg-config fuse
```

CentOS 6.5+:

```
sudo yum install automake gcc-c++ git libcurl-devel libxml2-devel \
                 fuse-devel make openssl-devel fuse
```

macOS:

```shell
brew install automake git curl libxml2 make pkg-config openssl 
brew cask install osxfuse
```
 
如果在编译过程中遇到提示fuse版本低于2.8.4，请参考常见问题来解决

然后您可以在github上下载源码并编译安装：

```
git clone https://github.com/XXX/cosfs.git
cd cosfs
./autogen.sh
./configure
make
sudo make install
```

### 运行

设置bucket name, access key/id信息，将其存放在/etc/passwd-cosfs 文件中，
注意这个文件的权限必须正确设置，建议设为640。

```
echo bucket_name:my-access-key-id:my-access-key-secret > /etc/passwd-cosfs # bucket_name的格式类似bucketprefix-123456789
chmod 640 /etc/passwd-cosfs
```

将cos bucket mount到指定目录
```
cosfs bucket_name my-mount-point -ourl=my-cos-endpoint
```

注意：

v1.0.5 版本之前的cosfs挂载命令：
``
cosfs bucketname_suffix:bucketname_prefix my-mount-point -ourl=my-cos-endpoint
``

v1.0.5 版本之前的配置文件格式是：
``
bucketname_prefix:<SecretId>:<SecretKey>
``
其中`bucketname_suffix`指的是bucket名称中的数字后缀, `bucketname_prefix`指的是除数字后缀外的其他部分。
例如 bucketprefix-1253972369 的`bucketname_suffix` 为1253972369， `bucketname_prefix`为bucketprefix。

#### 示例

将`my-bucket`这个bucket挂载到`/tmp/cosfs`目录下，AccessKeyId是`faint`，
AccessKeySecret是`123`，cos endpoint是`http://cos.ap-guangzhou.myqcloud.com`
```
echo my-bucket:faint:123 > /etc/passwd-cosfs
chmod 640 /etc/passwd-cosfs
mkdir /tmp/cosfs
cosfs my-bucket /tmp/cosfs -ourl=http://cos.ap-guangzhou.myqcloud.com -odbglevel=info -ouse_cache=/path/to/local_cache
```
-ouse_cache 指定了使用本地cache来缓存临时文件，进一步提高性能，如果不需要本地cache或者本地磁盘容量有限，可不指定该选项

卸载bucket:

```bash
fusermount -u /tmp/cosfs # non-root user
```


如何使用STS（临时密钥）来挂载bucket

```
参考挂载示例
./cosfs rabbitliu-1252448703 /mnt/rabbit/ -ourl=http://cos.ap-guangzhou.myqcloud.com -odbglevel=info -oallow_other -ocam_role=sts -opasswd_file=/tmp/passwd-jimmy-sts

其中 -ocam_role=sts 是必须的参数

-opasswd_file 含义和之前一致，文件名和路径可以任意指定
但是文件内容需要以以下格式提供
COSAccessKeyId=AK**********************
COSSecretKey=**************************
COSAccessToken=***************************
COSAccessTokenExpire=2017-08-29T20:30:00

其中COSAccessTokenExpire 代表临时token过期时间，为GMT时间，格式需要和例子中一致
cosfs会根据这个时间来判断是否要重新加载配置来获取到最新的配置

其它三个参数需要向CAM申请获取


```

### 局限性

cosfs提供的功能和性能和本地文件系统相比，具有一些局限性。具体包括：

* 随机或者追加写文件会导致整个文件的重写。
* 元数据操作，例如list directory，性能较差，因为需要远程访问COS服务器。
* 文件/文件夹的rename操作不是原子的。
* 多个客户端挂载同一个COS bucket时，依赖用户自行协调各个客户端的行为。例如避免多个客户端写同一个文件等等。
* 不支持hard link。
* 不适合用在高并发读/写的场景，这样会让系统的load升高


### 相关链接

* [s3fs](https://github.com/s3fs-fuse/s3fs-fuse) - 通过fuse接口，mount s3 bucket到本地文件系统。


### License

Licensed under the GNU GPL version 2

### 常见问题
如果您在使用 COSFS 工具过程中，有相关的疑问，请参阅[常见问题](https://github.com/tencentyun/cosfs/wiki/%E5%B8%B8%E8%A7%81%E9%97%AE%E9%A2%98)
