#include "server.h"
#include <sys/socket.h>
#include <arpa/inet.h>//sockaddr_in头文件
#include <sys/epoll.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>//用于recv报错的时候
#include <strings.h>//strcasecmp字符串比较的头文件
#include <string.h>
#include <sys/stat.h>//判断文件属性头文件
#include <sys/sendfile.h>//sendfile的头文件
#include <unistd.h>//close的头文件
#include <assert.h>//断言，弹出错误就挂掉
#include <dirent.h>//scandir的头文件
#include <stdlib.h>//free的头文件夹
#include <ctype.h>//isxdigit头文件
#include <stdio.h>//包含perror的头文件

typedef struct InfoFd
{
	int fd;
	int epfd;
	pthread_t tid;
}InfoFd;

int initListenFd(unsigned short port)
{
	//创建socket
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd == -1)
	{
		perror("socket");
		return -1;
	}

	//设置端口复用
	//lfd可以复用绑定的端口，如果主动断开，大约会等待一分钟，一分钟以后才会主动释放
	//绑定的端口，这一分钟内是不能再使用端口的。设置端口复用就是一分钟内可以再次使用
	int opt = 1;
	int ret = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	if (ret == -1)
	{
		perror("setsockopt");
		return -1;
	}

	//绑定socekt
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

	//设置监听
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
	//创建epoll
	int epfd = epoll_create(1);//参数大于即可
	if (epfd == -1)
	{
		perror("epoll_create");
		return -1;
	}

	//上树
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
	//检测
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
				//建立连接
				pthread_create(&info->tid, NULL, acceptClient, info);/******************/
			}
			else
			{
				//通讯
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
	//设置为非阻塞模式
	int flag = fcntl(cfd, F_GETFL);//F_GETFL获取文件描述符状态
	flag |= O_NONBLOCK;//非阻塞模式
	fcntl(cfd, F_SETFL, flag);//F_SETFL修改文件描述符状态

	struct epoll_event ev;
	ev.data.fd = cfd;
	ev.events = EPOLLIN | EPOLLET;//边沿模式
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
	printf("开始接收数据...\n");
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
	if (len == -1 && errno == EAGAIN)//EAGAIN连续读但没有数据
	{
		//解析请求行
		char * pt = strstr(buf, "\r\n");
		int len = pt - buf;
		buf[len] = '\0';
		parseRequestLine(buf, info->fd);

	}
	else if(len == 0)
	{
		//断开连接
		epoll_ctl(info->epfd, EPOLL_CTL_DEL, info->fd, NULL);
		close(info->fd);
	}
	else
	{
		//错误
		perror("recv");
		return NULL;
	}
	printf("recvMsg threadID:%ld\n", info->tid);
	free(info);
	return NULL;
}

void parseRequestLine(const char * buf, int cfd)
{
	//解析请求行  get  /xxx/1.jpg  http/1.1
	char request[24] = { 0 };
	char path[1024] = { 0 };

	sscanf(buf, "%[^ ] %[^ ]", request, path);
	printf("method:%s,path:%s\n", request, path);
	if (strcasecmp(request, "get") != 0)//strcasecmp字符串比较，不区分大小写
	{
		return;
	}
	decodeMsg(path, path);//解析中文路径

	//处理客户端请求的静态资源（目录或文件）
	char * file = NULL;
	if (strcmp(path, "/") == 0)//此时path在当前目录
	{
		file = "./";
	}
	else//path不在当前目录
	{
		//此时path为/xxx/1.jpg，将path向后移一位为xxx/1.jpg
		file = path + 1;
	}

	struct stat st;
	int ret = stat(file, &st);
	if (ret == -1)//文件不存在
	{
		sendMsgHead(cfd, 404, "Not Found", getFileType(".html"), -1);
		sendFile("404.html", cfd);
		return;
	}
	if (S_ISDIR(st.st_mode))//目录
	{
		//把本地目录中的内容发送给客户端
		sendMsgHead(cfd, 200, "OK", getFileType(".html"), -1);
		sendDir(file, cfd);
	}
	else//文件
	{
		//把文件中内容发送给客户端
		sendMsgHead(cfd, 200, "OK", getFileType(file), st.st_mode);
		sendFile(file, cfd);	
	}
	
	return ;
}

int sendFile(const char * fileName, int cfd)
{
	int fd = open(fileName, O_RDONLY);
	assert(fd > 0);
#if 0//这种方式较为复杂
	while (1)//读数据
	{
		char buf[1024];
		int len = read(fd, buf, sizeof(buf));
		if (len > 0)
		{
			send(cfd, buf, len, 0);
			//客户端发送数据很快，但是浏览器接收到数据，需要解析，就比较慢。如果浏览器接收到太多
			//数据，就会解析不过来，就会出问题，因此需要慢一点发
			usleep(10);//很重要--------------------------------------------------------
		}
		else if (len == 0)//文件读完了
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
		//此函数号称0拷贝，不需要多余的内存空间，效率高
		sendfile(cfd, fd, &offset, size - offset);//参数1输出的文件描述符，参数2输入的文件描述符，参数3偏移量
	}
	close(fd);
	return 0;
}

int sendDir(const char * fileName, int cfd)
{
//html格式
/*
<html>
	<head>
		<title>test</title>
	</head>
	<body>
		<table>
			<tr>//行
				<td></td>//列
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
	//scandir对目录搜索得到多少个文件，所有文件名保存在namelist中
	int num = scandir(fileName, &namelist, NULL, alphasort);

	for (int i = 0; i < num;i++)
	{
		//取出文件名   namelist指向指针数组 struct dirent *tmp[] 
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
	const char * dot = strchr(name, '.');//在name中找.
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

//把字符串转换成整型（16进制-10进制）
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

//to存储解码后的数据，传出参数，from被解码的数据，传入参数
void decodeMsg(char * to, char *from)
{
	for (;*from != '\0';++to, ++from)
	{
		//isxdigit -> 判断字符是不是16进制个数，取值在0-f
		//Linux%E5%86%E6%A0.jpg
		if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2]))
		{
			//将16进制的数->10进制，将这个数值赋值给了字符  int -> char
			//将三个字符编程一个字符，这个字符就是原始数据
			*to = hexToDec(from[1] * 16 + hexToDec(from[2]));
			//跳过from[1]和from[2]，因为当前循环中已经处理过了
			from += 2;
		}
		else
		{
			*to = *from;
		}
	}
}