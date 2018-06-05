/*************************************************
这是一个半同步/半异步的进程池

(1)主进程监听socket,子进程（工作进程）连接socket

(2)每个子进程能同时处理多个客户连接

*************************************************/
#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

/*用于处理信号的管道，以实现统一事件源。后面称之为信号管道*/
static int sig_pipefd[2];

static int setnonblocking(int fd)
{
	int old_option = fcntl(fd, F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd, F_SETFL, new_option);
	return old_option;
}

static void addfd(int epollfd, int fd)
{
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	setnonblocking(fd);
}

/*从epollfd标志的epoll内核事件表中删除fd上的所有注册事件*/
static void removefd(int epollfd, int fd)
{
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
	close(fd);
}

static void sig_handler(int sig)
{
	int save_errno = errno;
	int msg = sig;
	send(sig_pipefd[1], (char*)&msg, 1, 0);
	errno = save_errno;
}

static void addsig(int sig, void(handler)(int), bool restart = true)
{
	struct sigaction sa;
	memset(&sa, '\0', sizeof(sa));
	sa.sa_handler = handler;
	if (restart)
	{
		sa.sa_flags |= SA_RESTART;
	}
	sigfillset(&sa.sa_mask);
	assert(sigaction(sig, &sa, NULL) != -1);
}

/*描述一个子进程的类，m_pid是目标子进程的PID，m_pipefd是父进程和子进程通信用的管道 */
class process
{
public:
	process() : m_pid(-1){}
public:
	pid_t m_pid;
	int m_pipefd[2];
};

/*进程池类，将它定义为模板类是为了代码复用，其模板参数是处理逻辑任务的类cgi_conn */
template< typename T>
class processpool
{
private:
	/*单例模式*/
	/*将构造函数定位为私有的，我们只能通过后面的create静态函数来创建processpool实例 */
	/*这里是声明，不分配内存*/
	/*默认参数只能在函数声明中设定一次。如果没有函数声明，才可以在函数定义中设定*/
	processpool(int listenfd, int process_number = 8);
public:
	/*单例模式，实例化接口，保证程序最多创建一个processpool实例，这是程序正确处理的信号的必要条件*/
	/*这里是声明，不分配内存*/
	/*默认参数只能在函数声明中设定一次。如果没有函数声明，才可以在函数定义中设定*/
	static processpool<T>* create(int listenfd, int process_number = 8);
	/*析构函数*/
	~processpool();
	/*启动进程池*/
	void run();
private:
	void setup_sig_pipe();
	void run_parent();
	void run_child();
private:
	/*进程池允许的最大子进程数量 */
	static const int MAX_PROCESS_NUMBER = 16;

	/*每个子进程最多能处理的客户数量 */
	static const int USER_PER_PROCESS = 65535;

	/*epool最多能处理的事件数量  */
	static const int MAX_EVENT_NUMBER = 10000;

	/*进程池中进程总数 */
	int m_process_number;

	/*子进程在池中的序号，从0开始 */
	int m_idx;

	/*每个进程都有一个epool内核事件表，用m_epollfd标识 */
	int m_epollfd;

	/*监听socket */
	int m_listenfd;

	/*子进程通过m_stop来决定是都停止运行 */
	int m_stop;

	/*保存所有子进程的描述信息 */
	process* m_sub_process;

	/*单例模式，进程池静态实例*/
	/*这里是声明，不分配内存*/
	static processpool<T>* m_instance;
};

/*构造函数，参数listenfd是监听socket，它必须在创建进程池之前被创建，否则子进程无法直接引用它。*/
/*参数process_number指定进程池中子进程的数量*/
template< typename T>
processpool<T>::processpool(int listenfd, int process_number)
:m_listenfd(listenfd), m_process_number(process_number), m_idx(-1), m_stop(false)
{
	assert((process_number > 0) && (process_number <= MAX_PROCESS_NUMBER));

	m_sub_process = new process[process_number];
	assert(m_sub_process);

	/*创建process_number个子进程， 并建立它们和父进程之间的管道*/
	for (int i = 0; i < process_number; i++)
	{
		int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd);
		assert(ret == 0);

		m_sub_process[i].m_pid = fork();
		assert(m_sub_process[i].m_pid >= 0);
		if (m_sub_process[i].m_pid > 0)
		{
			close(m_sub_process[i].m_pipefd[1]);
			continue;
		}
		else
		{
			close(m_sub_process[i].m_pipefd[0]);
			m_idx = i;
			break;
		}
	}
}

/*单例模式，进程池静态实例，这里是定义，分配内存*/
template< typename T>
processpool<T>* processpool<T>::m_instance = NULL;

/*单例模式，实例化接口，保证程序最多创建一个processpool实例，这是程序正确处理的信号的必要条件*/
/*这里是定义，分配内存*/
template< typename T>
processpool<T>* processpool<T>::create(int listenfd, int process_number)
{
	if (!m_instance)
	{
		m_instance = new processpool<T> (listenfd, process_number);
	}
	return m_instance;
}

/*析构函数*/
template< typename T>
processpool<T>::~processpool()
{
	delete [] m_sub_process;
}

/*统一事件源*/
template< typename T>
void processpool<T>::setup_sig_pipe()
{
	/*创建epoll事件监听表和信号管道*/
	m_epollfd = epoll_create(5);
	assert(m_epollfd != -1);

	int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
	assert(ret != -1);

	setnonblocking(sig_pipefd[1]);
	addfd(m_epollfd, sig_pipefd[0]);

	/*设置信号处理函数*/
	addsig(SIGCHLD, sig_handler);
	addsig(SIGTERM, sig_handler);
	addsig(SIGINT, sig_handler);
	addsig(SIGPIPE, SIG_IGN);
}

/*父进程中m_idx值为-1， 子进程中m_idx值大于等于0， 我们据此判断下来要运行的是父进程代码还是子进程代码*/
template< typename T>
void processpool<T>::run()
{
	if (m_idx != -1)
	{
		run_child();
		return;
	}
	run_parent();
}

/*运行子进程*/
template<typename T>
void processpool<T>::run_child()
{
	setup_sig_pipe();

    /*每个子进程都通过其在进程池中的序号值m_idx找到与父进程通信的管道*/
    int pipefd = m_sub_process[m_idx].m_pipefd[1];
    /*子进程需要监听管道文件描述符pipefd， 因为父进程将通过它来通知子进程accept新连接*/
    addfd(m_epollfd, pipefd);

    epoll_event events[MAX_EVENT_NUMBER];
    T* users = new T[USER_PER_PROCESS];
    assert(users);
    int number = 0;
    int ret = -1;

    while (! m_stop)
    {
    	number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
    	if ((number < 0) && (errno != EINTR))
    	{
    		printf("epoll failure\n");
    		break;
    	}

    	for (int i = 0; i < number; i++)
    	{
    		int sockfd = events[i].data.fd;
    		if ((sockfd == pipefd) && (events[i].events & EPOLLIN))
    		{
    			int client = 0;
    			/*从父、子进程之间的管道读取数据，并将结果保存在变量client中，如果读取成功，则表示有新客户连接到来*/
    			ret = recv(sockfd, (char*)&client, sizeof(client), 0);
    			if ( ( (ret < 0) && (errno != EAGAIN) ) || ret == 0)
    			{
    				continue;
    			}
    			else
    			{
    				struct sockaddr_in client_address;
    				socklen_t client_addrlength = sizeof(client_address);
    				int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, 
    					&client_addrlength);
    				if (connfd < 0)
    				{
    					printf("errno is:%d\n", errno);
    					continue;
    				}
    				addfd(m_epollfd, connfd);
    				/*模板类T必须实现init方法，以初始化一个客户连接，我们直接使用connfd来索引逻辑处理对象
    				(T类型对象)，以提高程序效率*/
    				users[connfd].init(m_epollfd, connfd, client_address);
    			}
    		}
    		/*下面处理子进程接收到的信号*/
    		else if ((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN))
    		{
    			int sig;
    			char signals[1024];
    			ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
    			if (ret <= 0)
    			{
    				continue;
    			}
    			else
    			{
    				for (int i = 0; i < ret; i++)
    				{
    					switch (signals[i])
    					{
    						case SIGCHLD:
    						{
    							pid_t pid;
    							int stat;
    							while ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
    							{
    								continue;
    							}
    							break;
    						}
    						case SIGTERM:
    						case SIGINT:
    						{
    							m_stop = true;
    							break;
    						}
    						default:
    						{
    							break;
    						}
    					}
    				}
    			}
    		}
    		/*如果时其他可读数据，那么必然是客户请求到来，调用逻辑处理对象的process方法处理之*/
    		else if (events[i].events & EPOLLIN)
    		{
    			users[sockfd].process();
    		}
    		else
    		{
    			continue;
    		}
    	}
    }
    delete [] users;
    users = NULL;
    close(pipefd);

    /*我们将这一句注释掉，提醒：应该由m_listenfd的创建者来关闭这个文件描述符。
    对象由哪个函数创建，就应该由哪个函数销毁
	main函数创建了文件描述符listenfd，那么就由main函数亲自关闭它*/
	
	//close(m_listenfd);

    close(m_epollfd);
}

/*运行父进程*/
template< typename T>
void processpool<T>::run_parent()
{
	setup_sig_pipe();

	/*父进程监听m_listenfd*/
	addfd(m_epollfd, m_listenfd);

	epoll_event events[MAX_EVENT_NUMBER];
	int sub_process_counter = 0;
	int new_conn = 1;
	int number = 0;
	int ret = -1;

	while (! m_stop)
	{
		number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
		if ((number < 0) && (errno != EINTR))
		{
			printf("epoll failure\n");
			break;
		}
		for (int i = 0; i < number; i++)
		{
			int sockfd = events[i].data.fd;
			if (sockfd == m_listenfd)
			{
				/*如果有新连接到来， 就采用Round Robin方式将其分配给一个子进程处理*/
				int i = sub_process_counter;
				do
				{
					if (m_sub_process[i].m_pid != -1)
					{
						break;
					}
					i = (i+1) % m_process_number;
				}while(i != sub_process_counter);
				if (m_sub_process[i].m_pid == -1)
				{
					m_stop = true;
					break;
				}
				sub_process_counter = (i+1) % m_process_number;
				send(m_sub_process[i].m_pipefd[0], (char*)&new_conn, sizeof(new_conn), 0);
				printf("send requests to child %d\n", i);
			}

			else if((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN))
			{
				int sig;
				char signals[1024];
				ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
				if (ret <= 0)
				{
					continue;
				}
				else
				{
					for (int i = 0; i < ret; i++)
					{
						switch(signals[i])
						{
							case SIGCHLD:
							{
								pid_t pid;
								int stat;
								while ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
								{
									for (int i = 0; i < m_process_number; i++)
									{
										/*如果进程池中第i个子进程退出了， 则主进程关闭相应的通信管道，
										并设置相应的m_pid为-1， 以标记该子进程已退出*/
										if (m_sub_process[i].m_pid == pid)
										{
											printf("child %d join\n", i);
											close(m_sub_process[i].m_pipefd[0]);
											m_sub_process[i].m_pid = -1;
										}
									}
								}
								/*如果所有子进程都已经退出了，则父进程也退出*/
								m_stop = true;
								for (int i = 0; i < m_process_number; i++)
								{
									if (m_sub_process[i].m_pid != -1)
									{
										m_stop = false;
									}
								}
								break;
							}
							case SIGTERM:
							case SIGINT:
							{
								/*如果父进程接收到终止信号，那么就杀死所有子进程，并等待它们全部结束。
								当然， 通知子进程结束更好的方法时向父、子进程之间的通信管道发送特殊数据。*/
								printf("kill all the chlid now\n");
								for (int i = 0; i < m_process_number; i++)
								{
									int pid = m_sub_process[i].m_pid;
									if (pid != -1)
									{
										kill(pid, SIGTERM);
									}
								}
								break;
							}
							default:
							{
								break;
							}
						}
					}
				}
			}
			else
			{
				continue;
			}
		}
	}

	/*我们将这一句注释掉，提醒：应该由m_listenfd的创建者来关闭这个文件描述符。
    对象由哪个函数创建，就应该由哪个函数销毁
	main函数创建了文件描述符listenfd，那么就由main函数亲自关闭它*/
	
	//close(m_listenfd);

	close(m_epollfd);
}

#endif