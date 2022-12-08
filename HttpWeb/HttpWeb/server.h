#pragma once

//����socketͨ��
int initListenFd(unsigned short port);
//����epollģ��
int epollRun(int lfd);
//��������
void * acceptClient(void * arg);
//����ͨѶ
void * recvHttpRequest(void * arg);
//����������
void parseRequestLine(const char * buf, int cfd);
//����·��
int sendFile(const char * fileName, int cfd);
//����Ŀ¼
int sendDir(const char * fileName, int cfd);
//������Ӧͷ
int sendMsgHead(int cfd, int status, const char *statusStr, const char *type, int length);
//��ʾ��Ϣ
const char * getFileType(const char * name);
//��������Ŀ¼
void decodeMsg(char * to, char *from);
//���ַ���ת�������ͣ�16����-10���ƣ�
int hexToDec(char c);