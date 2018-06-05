#ifndef CGI_H
#define CGI_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "processpool.h"

/*用于处理客户CGI请求的类，它可以作为processpool类的模板参数*/
class cgi_conn
{
public:
	cgi_conn(){}
	~cgi_conn(){}
	/*初始化客户连接，清空读缓冲区*/
	void init(int epollfd, int sockfd, const sockaddr_in &client_addr);
	void process();
private:
	/*读缓冲区的大小*/
	static const int BUFFER_SIZE = 1024;
	static int m_epollfd;
	int m_sockfd;
	sockaddr_in m_address;
	/*读缓冲区*/
	char m_buf[BUFFER_SIZE];
	/*标记读缓冲区中已经读入的客户数据的最后一个字节的下一个位置*/
	int m_read_idx;
};

int cgi_conn::m_epollfd = -1;

#endif