#include "cgi.h"

/*初始化客户连接，清空读缓冲区*/
void cgi_conn::init(int epollfd, int sockfd, const sockaddr_in &client_addr)
{
	m_epollfd = epollfd;
	m_sockfd  = sockfd;
	m_address = client_addr;
	memset(m_buf, '\0', BUFFER_SIZE);
	m_read_idx = 0;
}

void cgi_conn::process()
{
	int idx = 0;
	int ret = -1;
	/*循环读取和分析客户数据*/
	while (true)
	{
		idx = m_read_idx;

		/*
		参数说明：
		m_sockfd 指定接收端套接字描述符
		m_buf + idx 指明一个缓冲区，用来存放接收到的数据
		BUFFER_SIZE - 1 - idx　指明缓冲区的长度
		*/
		ret = recv(m_sockfd, m_buf + idx, BUFFER_SIZE - 1 - idx, 0);
		/*如果读操作发生错误，则关闭客户连接，但如果是暂时无数据可读，则退出循环*/
		if (ret < 0)
		{
			if (errno != EAGAIN)
			{
				removefd(m_epollfd, m_sockfd);
			}
			break;
		}
		/*如果对方关闭连接，则服务器也关闭连接*/
		else if (ret == 0)
		{
			removefd(m_epollfd, m_sockfd);
			break;
		}
		else
		{
			m_read_idx += ret;
			printf("user content is : %s\n", m_buf);
			/*如果遇到字符"\r\n"，则开始处理客户请求*/
			for (; idx < m_read_idx; idx++)
			{
				if ((idx >= 1) && (m_buf[idx - 1] == '\r') && (m_buf[idx] == '\n'))
				{
					break;
				}
			}
			/*如果没有遇到字符"\r\n"，则需要读取更多客户数据*/
			if (idx == m_read_idx)
			{
				continue;
			}
			m_buf[idx - 1] = '\0';
			char* file_name = m_buf;
			/*判断客户要运行的CGI程序是否存在*/
			if (access(file_name, F_OK) == -1)
			{
				/*CGI程序不存在*/
				removefd(m_epollfd, m_sockfd);
				break;
			}
			/*创建子进程来执行CGI程序*/
			ret = fork();
			if (ret == -1)
			{
				removefd(m_epollfd, m_sockfd);
				break;
			}
			else if (ret > 0)
			{
				/*父进程只需要关闭连接*/
				removefd(m_epollfd, m_sockfd);
				break;
			}
			else
			{
				/*子进程将标准输出定向到m_sockfd, 并执行CGI程序*/
				close(STDOUT_FILENO);
				dup(m_sockfd);
				execl(m_buf, m_buf, NULL);
				exit(0);
			}
		}
	}
}

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
	assert(listenfd != 0);

	int ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
	assert(ret != -1);

	ret = listen(listenfd, 5);
	assert(ret != -1);

	/*进程池在这里使用*/
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