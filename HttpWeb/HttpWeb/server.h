#pragma once

//建立socket通信
int initListenFd(unsigned short port);
//创建epoll模型
int epollRun(int lfd);
//建立连接
void * acceptClient(void * arg);
//建立通讯
void * recvHttpRequest(void * arg);
//解析请求行
void parseRequestLine(const char * buf, int cfd);
//发送路径
int sendFile(const char * fileName, int cfd);
//发送目录
int sendDir(const char * fileName, int cfd);
//发送响应头
int sendMsgHead(int cfd, int status, const char *statusStr, const char *type, int length);
//提示信息
const char * getFileType(const char * name);
//解码中文目录
void decodeMsg(char * to, char *from);
//把字符串转换成整型（16进制-10进制）
int hexToDec(char c);