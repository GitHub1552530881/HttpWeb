#include "server.h"
#include <sys/socket.h>
#include <arpa/inet.h>//sockaddr_inͷ�ļ�
#include <sys/epoll.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>//����recv�����ʱ��
#include <strings.h>//strcasecmp�ַ����Ƚϵ�ͷ�ļ�
#include <string.h>
#include <sys/stat.h>//�ж��ļ�����ͷ�ļ�
#include <sys/sendfile.h>//sendfile��ͷ�ļ�
#include <unistd.h>//close��ͷ�ļ�
#include <assert.h>//���ԣ���������͹ҵ�
#include <dirent.h>//scandir��ͷ�ļ�
#include <stdlib.h>//free��ͷ�ļ���
#include <ctype.h>//isxdigitͷ�ļ�
#include <stdio.h>//����perror��ͷ�ļ�

typedef struct InfoFd
{
	int fd;
	int epfd;
	pthread_t tid;
}InfoFd;

int initListenFd(unsigned short port)
{
	//����socket
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd == -1)
	{
		perror("socket");
		return -1;
	}

	//���ö˿ڸ���
	//lfd���Ը��ð󶨵Ķ˿ڣ���������Ͽ�����Լ��ȴ�һ���ӣ�һ�����Ժ�Ż������ͷ�
	//�󶨵Ķ˿ڣ���һ�������ǲ�����ʹ�ö˿ڵġ����ö˿ڸ��þ���һ�����ڿ����ٴ�ʹ��
	int opt = 1;
	int ret = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	if (ret == -1)
	{
		perror("setsockopt");
		return -1;
	}

	//��socekt
	struct sockaddr_in addr;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);
	addr.sin_family = AF_INET;
	ret = bind(lfd, (struct sockaddr*)&addr, sizeof(addr));
	if (ret == -1)
	{
		perror("bind");
		return -1;
	}

	//���ü���
	ret = listen(lfd, 128);
	if (ret == -1)
	{
		perror("listen");
		return -1;
	}
	return lfd;
}

int epollRun(int lfd)
{
	//����epoll
	int epfd = epoll_create(1);//�������ڼ���
	if (epfd == -1)
	{
		perror("epoll_create");
		return -1;
	}

	//����
	struct epoll_event ev;
	ev.data.fd = lfd;
	ev.events = EPOLLIN;
	int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
	if (ret == -1)
	{
		perror("epoll_ctl");
		return -1;
	}
	printf("ret = %d\n",ret);
	//���
	struct epoll_event evs[1024];
	int len = sizeof(evs) / sizeof(struct epoll_event);
	printf("len = %d\n", len);
	while (1)
	{
		int num = epoll_wait(epfd, evs, len, -1);
		printf("num = %d\n",num);
		for (int i = 0; i < num; i++)
		{
			printf("i = %d\n", i);
			int fd = evs[i].data.fd;
			InfoFd * info = (InfoFd*)malloc(sizeof(InfoFd));
			info->epfd = epfd;
			info->fd = fd;
			if (fd == lfd)
			{
				//��������
				pthread_create(&info->tid, NULL, acceptClient, info);/******************/
			}
			else
			{
				//ͨѶ
				pthread_create(&info->tid, NULL, recvHttpRequest, info);
			}
		}
	}
	return 0;
}

void * acceptClient(void * arg)
{
	InfoFd * info = (InfoFd*)arg;
	printf("qqqq\n");
	int cfd = accept(info->fd, NULL, NULL);
	if (cfd == -1)
	{
		perror("accept");
		return NULL;
	}
	printf("qqqq\n");
	//����Ϊ������ģʽ
	int flag = fcntl(cfd, F_GETFL);//F_GETFL��ȡ�ļ�������״̬
	flag |= O_NONBLOCK;//������ģʽ
	fcntl(cfd, F_SETFL, flag);//F_SETFL�޸��ļ�������״̬

	struct epoll_event ev;
	ev.data.fd = cfd;
	ev.events = EPOLLIN | EPOLLET;//����ģʽ
	int ret = epoll_ctl(info->epfd, EPOLL_CTL_ADD, cfd, &ev);
	if (ret == -1)
	{
		perror("epoll_ctl");
		return NULL;
	}
	printf("acceptClient threadID:%ld\n", info->tid);
	free(info);
	return NULL;
}

void * recvHttpRequest(void * arg)
{
	printf("��ʼ��������...\n");
	InfoFd * info = (InfoFd *)arg;

	char buf[4096] = { 0 };
	char tmp[1024] = { 0 };
	int len = 0;
	int total = 0;
	while ((len = recv(info->fd, tmp, sizeof(tmp), 0)) > 0)
	{
		if (total + len < sizeof(buf))
		{
			memcpy(buf + total, tmp, len);
		}
		total += len;
	}
	if (len == -1 && errno == EAGAIN)//EAGAIN��������û������
	{
		//����������
		char * pt = strstr(buf, "\r\n");
		int len = pt - buf;
		buf[len] = '\0';
		parseRequestLine(buf, info->fd);

	}
	else if(len == 0)
	{
		//�Ͽ�����
		epoll_ctl(info->epfd, EPOLL_CTL_DEL, info->fd, NULL);
		close(info->fd);
	}
	else
	{
		//����
		perror("recv");
		return NULL;
	}
	printf("recvMsg threadID:%ld\n", info->tid);
	free(info);
	return NULL;
}

void parseRequestLine(const char * buf, int cfd)
{
	//����������  get  /xxx/1.jpg  http/1.1
	char request[24] = { 0 };
	char path[1024] = { 0 };

	sscanf(buf, "%[^ ] %[^ ]", request, path);
	printf("method:%s,path:%s\n", request, path);
	if (strcasecmp(request, "get") != 0)//strcasecmp�ַ����Ƚϣ������ִ�Сд
	{
		return;
	}
	decodeMsg(path, path);//��������·��

	//����ͻ�������ľ�̬��Դ��Ŀ¼���ļ���
	char * file = NULL;
	if (strcmp(path, "/") == 0)//��ʱpath�ڵ�ǰĿ¼
	{
		file = "./";
	}
	else//path���ڵ�ǰĿ¼
	{
		//��ʱpathΪ/xxx/1.jpg����path�����һλΪxxx/1.jpg
		file = path + 1;
	}

	struct stat st;
	int ret = stat(file, &st);
	if (ret == -1)//�ļ�������
	{
		sendMsgHead(cfd, 404, "Not Found", getFileType(".html"), -1);
		sendFile("404.html", cfd);
		return;
	}
	if (S_ISDIR(st.st_mode))//Ŀ¼
	{
		//�ѱ���Ŀ¼�е����ݷ��͸��ͻ���
		sendMsgHead(cfd, 200, "OK", getFileType(".html"), -1);
		sendDir(file, cfd);
	}
	else//�ļ�
	{
		//���ļ������ݷ��͸��ͻ���
		sendMsgHead(cfd, 200, "OK", getFileType(file), st.st_mode);
		sendFile(file, cfd);	
	}
	
	return ;
}

int sendFile(const char * fileName, int cfd)
{
	int fd = open(fileName, O_RDONLY);
	assert(fd > 0);
#if 0//���ַ�ʽ��Ϊ����
	while (1)//������
	{
		char buf[1024];
		int len = read(fd, buf, sizeof(buf));
		if (len > 0)
		{
			send(cfd, buf, len, 0);
			//�ͻ��˷������ݺܿ죬������������յ����ݣ���Ҫ�������ͱȽ����������������յ�̫��
			//���ݣ��ͻ�������������ͻ�����⣬�����Ҫ��һ�㷢
			usleep(10);//����Ҫ--------------------------------------------------------
		}
		else if (len == 0)//�ļ�������
		{
			break;
		}
		else
		{
			perror("read");
		}
	}
#endif
	off_t offset = 0;
	int size = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	while (offset < size)
	{
		//�˺����ų�0����������Ҫ������ڴ�ռ䣬Ч�ʸ�
		sendfile(cfd, fd, &offset, size - offset);//����1������ļ�������������2������ļ�������������3ƫ����
	}
	close(fd);
	return 0;
}

int sendDir(const char * fileName, int cfd)
{
//html��ʽ
/*
<html>
	<head>
		<title>test</title>
	</head>
	<body>
		<table>
			<tr>//��
				<td></td>//��
				<td></td>
			</tr>
			<tr>
				<td></td>
				<td></td>
			</tr>
		</table>
	</body>
</html>
*/
	char buf[4096] = { 0 };
	sprintf(buf, "<html><head><title>%s</title></head><body><table>", fileName);

	struct dirent **namelist;
	//scandir��Ŀ¼�����õ����ٸ��ļ��������ļ���������namelist��
	int num = scandir(fileName, &namelist, NULL, alphasort);

	for (int i = 0; i < num;i++)
	{
		//ȡ���ļ���   namelistָ��ָ������ struct dirent *tmp[] 
		char * name = namelist[i]->d_name;
		struct stat st;
		char subPath[1024] = { 0 };
		sprintf(subPath, "%s/%s", fileName, name);
		stat(subPath, &st);//***************************************
		if (S_ISDIR(st.st_mode))
		{
			sprintf(buf + strlen(buf), "<tr><td><a href=\"%s/\">%s</a></td><td>%ld</td></tr>",
				name, name, st.st_size);
		}
		else
		{
			sprintf(buf + strlen(buf), "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",
				name, name, st.st_size);
		}
		send(cfd, buf, strlen(buf), 0);
		memset(buf, 0, sizeof(buf));
		free(namelist[i]);
	}
	sprintf(buf, "</table></body></html>");
	send(cfd, buf, strlen(buf), 0);
	free(namelist);
	return 0;
}

int sendMsgHead(int cfd, int status, const char * statusStr, const char * type, int length)
{
	char buf[4096] = { 0 };
	sprintf(buf, "http/1.1 %d %s\r\n", status, statusStr);
	sprintf(buf + strlen(buf), "content-type:%s\r\n", type);
	sprintf(buf + strlen(buf), "content-length:%d\r\n", length);//**************************
	sprintf(buf + strlen(buf), "\r\n");

	send(cfd, buf, sizeof(buf), 0);
	return 0;
}

const char * getFileType(const char * name)
{
	const char * dot = strchr(name, '.');//��name����.
	if (dot == NULL)
		return "text/plain;charset=utf-8";
	if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
		return "text/html;charset=utf-8";
	if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
		return "image/jpeg";

	if (strcmp(dot, ".gif") == 0)
		return "image/gif";
	if (strcmp(dot, ".png") == 0)
		return "image/png";
	if (strcmp(dot, ".ccs") == 0)
		return "text/ccs";
	if (strcmp(dot, ".au") == 0)
		return "audio/basic";
	if (strcmp(dot, ".wav") == 0)
		return "audio/wav";
	if (strcmp(dot, ".avi") == 0)
		return "video/x-msvideo";
	if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
		return "video/quicktime";
	if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
		return "video/mpeg";
	if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
		return "model/vrml";
	if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
		return "audio/midi";
	if (strcmp(dot, ".mp3") == 0)
		return "audio/mpeg";
	if (strcmp(dot, ".ogg") == 0)
		return "application/ogg";
	if (strcmp(dot, ".pac") == 0)
		return "application/x-ns-proxy-autoconfig";

	return "text/plain;charset=utf-8";
}

//���ַ���ת�������ͣ�16����-10���ƣ�
int hexToDec(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return 0;
}

//to�洢���������ݣ�����������from����������ݣ��������
void decodeMsg(char * to, char *from)
{
	for (;*from != '\0';++to, ++from)
	{
		//isxdigit -> �ж��ַ��ǲ���16���Ƹ�����ȡֵ��0-f
		//Linux%E5%86%E6%A0.jpg
		if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2]))
		{
			//��16���Ƶ���->10���ƣ��������ֵ��ֵ�����ַ�  int -> char
			//�������ַ����һ���ַ�������ַ�����ԭʼ����
			*to = hexToDec(from[1] * 16 + hexToDec(from[2]));
			//����from[1]��from[2]����Ϊ��ǰѭ�����Ѿ��������
			from += 2;
		}
		else
		{
			*to = *from;
		}
	}
}