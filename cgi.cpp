#include "cgi.h"

int main(int argc, char* argv[])
{
	if (argc <= 2)
	{
		printf("usage: %s ip_address port_number\n", basename(argv[0]));
		exit(1);
	}
	const char* ip = argv[1];
	int port = atoi(argv[2]);
	
	struct sockaddr_in address;
	bzero(&address, sizeof(address));
	address.sin_family = AF_INET;
	inet_pton(AF_INET, ip, &address.sin_addr);
	address.sin_port = htons(port);

	int listenfd = socket(PF_INET, SOCK_STREAM, 0);
	assert(listenfd >= 0);

	int ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
	assert(ret != -1);

	ret = listen(listenfd, 5);
	assert(ret != -1);

	processpool<cgi_conn>* pool = processpool<cgi_conn>::create(listenfd);
	if (pool)
	{
		pool->run();
		delete pool;
	}

	/*正如前面提到，main函数创建了文件描述符listenfd，那么就由main函数亲自关闭它*/
	close(listenfd);
	exit(0);
}