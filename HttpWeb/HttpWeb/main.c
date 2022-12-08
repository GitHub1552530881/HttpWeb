#include <stdio.h>
#include "server.h"
#include <stdlib.h>
#include<unistd.h>//chdir的头文件

int main(int argc,char * argv[])
{
#if 0
	if (argc < 3)
	{
		printf("./a.out port path\n");
		return -1;
	}
	unsigned short port = atoi(argv[1]);
	chdir(argv[2]);
#else 
	chdir("/root/resource");
	unsigned short port = 20000;
#endif
	int lfd = initListenFd(port);
	epollRun(lfd);
    return 0;
}