#pragma once

#include "threadpool.hpp"
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
#include <poll.h>

#define BUFFER_SIZE 1024

class msg;
class client_connection;
class msg_quene;

int setnonblocking(int fd);

// 消息队列
class msg_quene {
    std::mutex mut;
    std::condition_variable cond;
    std::list<std::shared_ptr<msg>> que;

public:
    std::shared_ptr<msg> pop() {
        std::unique_lock<std::mutex> lk(mut);
        cond.wait(lk, [this] { return !que.empty(); });
        auto temp = que.front();
        que.pop_front();
        return temp;
    }

    void push_back(std::shared_ptr<msg> Msg) {
        std::lock_guard<std::mutex> lk(mut);
        que.push_back(Msg);
        cond.notify_one();
    }

    bool empty() {
        std::lock_guard<std::mutex> lk(mut);
        return que.empty();
    }
};

// 客户表，用于广播线程遍历已连接客户端
class client_map {
    std::mutex mut;
    std::unordered_map<int, client_connection *> thread_que_map;

    friend void traverse(std::shared_ptr<client_map> Msg_map, std::shared_ptr<msg> Msg);

public:
    void insert(int id, client_connection *client_conn) {
        std::lock_guard<std::mutex> lk(mut);
        thread_que_map[id] = client_conn;
        fprintf(stdout, "comes a new user, here are %d users now.\n", thread_que_map.size());
    }

    void erase(int id) {
        std::lock_guard<std::mutex> lk(mut);
        auto client_conn = thread_que_map[id];
        thread_que_map.erase(id);
        fprintf(stdout, "one user leaves, here are %d users now.\n", thread_que_map.size());
    }

    int size() {
        std::lock_guard<std::mutex> lk(mut);
        return thread_que_map.size();
    }

    client_connection *find(int id) {
        std::lock_guard<std::mutex> lk(mut);
        return thread_que_map[id];
    }

    // 用于在类外部进行遍历,满足基本可锁定 (BasicLockable)要求。
    void lock() { mut.lock(); }
    void unlock() { mut.unlock(); }

    ~client_map() { fprintf(stdout, "msg_map has been deconstructed.\n"); }
};

//消息类
class msg {
public:
    char user_name[20];
    char buf[BUFFER_SIZE];
    int user_id;

    msg() {
        bzero(user_name, 20);
        bzero(buf, BUFFER_SIZE);
    }

    ~msg() { fprintf(stdout, "msg from user %s : \'%s\' has been deconstructed\n", user_name, buf); }
};

// 客户连接类
class client_connection {
public:
    // 客户地址
    sockaddr_in client_address;
    // 客户连接待发送消息队列
    msg_quene msg_que;
    // 公共消息队列
    std::shared_ptr<msg_quene> pub_que;
    char user_name[20];

    std::shared_ptr<client_map> Msg_map;

    pollfd fds[1];

    int connfd;

    bool stop = false;

    client_connection(int _connfd, std::shared_ptr<client_map> _Msg_map, std::shared_ptr<msg_quene> _pub_que) : connfd(_connfd), Msg_map(_Msg_map), pub_que(_pub_que) { bzero(user_name, 20); }

    void push_back(std::shared_ptr<msg> Msg) { msg_que.push_back(Msg); }

    void process() {
        socklen_t client_addrlength = sizeof(client_address);
        connfd = accept(connfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0) {
            fprintf(stderr, "accept error, errno is:%d\n", errno);
        }

        Msg_map->insert(connfd, this);

        char temp[] = "欢迎来到多线程TCP聊天室！请输入您的用户名：";

        int ret = send(connfd, temp, strlen(temp), 0);
        assert(ret == strlen(temp));

        ret = recv(connfd, user_name, 20 - 1, 0);
        assert(ret > 0 || ret < 20);

        fds[0].fd = connfd;
        fds[0].events = POLLIN | POLLOUT | POLL_ERR | POLLRDHUP;

        stop = false;

        while (!stop) {
            ret = poll(fds, 1, 0);
            if (ret == 0) {
                send_to_client();
                continue;
            } else if (ret < 0) {
                fprintf(stderr, "poll failure\n");
                break;
            }
            if (fds[0].revents & POLLIN) {
                // 收到客户端发送的消息
                auto temp_msg = std::make_shared<msg>();
                memset(temp_msg->buf, '\0', BUFFER_SIZE);
                ret = recv(connfd, temp_msg->buf, BUFFER_SIZE - 1, 0);
                fprintf(stdout, "get %d bytes of client data %s from %d\n", ret, temp_msg->buf, connfd);
                if (ret < 0) {
                    if (errno != EAGAIN) {
                        close(connfd);
                        Msg_map->erase(connfd);
                        fprintf(stderr, "one user connection error, now here is %d users\n.", Msg_map->size());
                        return;
                    }
                } else if (ret == 0) {
                    leaves();
                } else {
                    temp_msg->user_id = connfd;
                    strcpy(temp_msg->user_name, user_name);
                    pub_que->push_back(temp_msg);
                }
            } else if (fds[0].revents & POLLOUT) {
                send_to_client();
            } else if (fds[0].revents & POLLRDHUP) {
                leaves();
            } else {
                error_ocurr();
            }
        }
    }

    // 向客户端发送消息
    void send_to_client() {
        while (!msg_que.empty()) {
            auto temp_msg = msg_que.pop();

            auto buf = temp_msg->buf;

            char temp[BUFFER_SIZE + 20];
            strcpy(temp, temp_msg->user_name);
            temp[strlen(temp) - 1] = ':';
            strcpy(temp + strlen(temp), buf);

            int ret = send(connfd, temp, strlen(temp), 0);
            if (ret == 0) {
                leaves();
            } else if (ret < 0) {
                error_ocurr();
            }
        }
    }

    // 客户端关闭连接
    void leaves() {
        close(connfd);
        Msg_map->erase(connfd);
        fprintf(stdout, "one user leaves, now here is %d users\n.", Msg_map->size());
        stop = true;
        return;
    }

    // 连接发生错误
    void error_ocurr() {
        close(connfd);
        Msg_map->erase(connfd);
        fprintf(stderr, "one user connection error, now here is %d users\n.", Msg_map->size());
        stop = true;
        return;
    }
};