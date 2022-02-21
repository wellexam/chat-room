#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdio>
#include <cerrno>
#include <fcntl.h>
#include <cstdlib>
#include <cassert>
#include <sys/epoll.h>
#include <csignal>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <list>
#include <memory>
#include <deque>

#define USER_LIMIT 5 /*最大用户数量*/
#define BUFFER_SIZE 1024
const int MAX_FD = 65536;           //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot);

//对文件描述符设置非阻塞
int setnonblocking(int fd);

//消息类
class msg {
public:
    char user_name[20];
    char buf[BUFFER_SIZE];

    msg() {
        bzero(user_name, 20);
        bzero(buf, BUFFER_SIZE);
    }

    ~msg() { fprintf(stdout, "msg from user %s : \'%s\' has been deconstructed\n", user_name, buf); }
};

//客户端数据类
class client {
public:
    sockaddr_in address;
    std::deque<std::shared_ptr<msg>> msg_que;
};

//客户端信息容器
using client_map = std::unordered_map<int, std::shared_ptr<client>>;
//未完成发送数据偏移量
using send_offset_map = std::unordered_map<int, int>;

//发送消息到客户端
void send_to_client(int epollfd, int connfd, client_map &clients, send_offset_map &sendbuff);

int main(int argc, char *argv[]) {

    int PORT = 65500;
    if (argc == 2) {
        PORT = atoi(argv[1]);
    }

    auto listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int ret = 0; // 返回值

    sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(PORT);

    int flag = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    // flag = 5;
    // setsockopt(listenfd, SOL_SOCKET, SO_SNDBUF, &flag, sizeof(flag));
    // socklen_t len = sizeof(flag);
    // getsockopt(listenfd, SOL_SOCKET, SO_SNDBUF, &flag, &len);
    // std::cout << flag << std::endl;
    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(listenfd, 5);
    assert(ret >= 0);

    epoll_event events[MAX_EVENT_NUMBER];
    auto epollfd = epoll_create(5);
    assert(epollfd != -1);

    addfd(epollfd, listenfd, false);

    //客户端信息容器
    client_map clients;

    //未完成发送数据偏移量
    send_offset_map sendbuff;

    //事件循环
    while (true) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, 0);
        if (number < 0 && errno != EINTR) {
            fprintf(stderr, "%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;

            //处理新到的客户连接
            if (sockfd == listenfd) {
                sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength);
                if (connfd < 0) {
                    fprintf(stderr, "accept error, errno is:%d\n", errno);
                    continue;
                }

                /*如果请求太多， 则关闭新到的连接 */
                if (clients.size() >= USER_LIMIT) {
                    const char *info = "too many users\n";
                    fprintf(stdout, "%s\n", info);
                    send(connfd, info, strlen(info), 0);
                    close(connfd);
                    continue;
                }

                clients[connfd] = std::make_shared<client>();
                clients[connfd]->address = client_address;
                addfd(epollfd, connfd, false);
                fprintf(stdout, "comes a new user, now here %d users\n", clients.size());

            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                //客户端关闭连接
                close(sockfd);
                clients.erase(sockfd);
                fprintf(stdout, "one user leaves, now here is %d users\n.", clients.size());
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN) {
                int connfd = sockfd;
                auto temp_msg = std::make_shared<msg>();
                while (true) {
                    ret = recv(connfd, temp_msg->buf, BUFFER_SIZE - 1, 0);
                    if (ret < 0) {
                        /*对于非阻塞IO，下的条件成立表示数据已经全部读取完毕。此后，
                         * epoll就能再次触发sockfd上的EPOLLIN事件，以驱动下一次读操作
                         */
                        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                            fprintf(stdout, "read later\n");
                            break;
                        } else {
                            close(connfd);
                            clients.erase(sockfd);
                            fprintf(stderr, "one user leaves, now here is %d users\n.", clients.size());
                        }
                    } else if (ret == 0) {
                        close(connfd);
                        clients.erase(sockfd);
                        fprintf(stdout, "one user connection error, now here is %d users\n.", clients.size());
                    } else {
                        fprintf(stdout, "get %d bytes of client data %s from %d\n", ret, temp_msg->buf, connfd);
                        for (auto &user : clients) {
                            if (user.first != connfd) {
                                clients[user.first]->msg_que.push_back(temp_msg);
                                send_to_client(epollfd, user.first, clients, sendbuff);
                            }
                        }
                    }
                }
            } else if (events[i].events & EPOLLOUT) {
                send_to_client(epollfd, sockfd, clients, sendbuff);
            }
        }
    }

    close(listenfd);
    return 0;
}

void send_to_client(int epollfd, int connfd, client_map &clients, send_offset_map &send_offset) {
    while (!clients[connfd]->msg_que.empty()) {
        auto buf = clients[connfd]->msg_que.front()->buf;
        auto sendbuff_pos = send_offset.find(connfd);
        if (sendbuff_pos != send_offset.end()) {
            buf += sendbuff_pos->second;
        }

        int ret = send(connfd, buf, strlen(buf), 0);
        if (ret >= 0 && ret < strlen(buf) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) {
            send_offset[connfd] += ret;
            addfd(epollfd, connfd, false);
            fprintf(stdout, "write later\n");
            break;
        } else if (ret == strlen(buf)) {
            clients[connfd]->msg_que.pop_front();
            send_offset.erase(connfd);
            continue;
        } else {
            close(connfd);
            clients.erase(connfd);
            send_offset.erase(connfd);
            fprintf(stderr, "one user connection error, now here is %d users\n.", clients.size());
        }
    }
}

void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;

    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLOUT;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}