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

#define BUFFER_SIZE 1024

class msg;
class client_connection;
class public_msg_que;

using msg_quene = std::deque<std::shared_ptr<msg>>;

// 公共消息队列
class public_msg_que {
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
};

// 用于广播线程遍历已连接客户端
class msg_map {
    std::mutex mut;
    std::unordered_map<int, client_connection *> thread_que_map;

    friend void traverse(std::shared_ptr<msg_map> Msg_map, std::shared_ptr<msg> Msg);

public:
    void insert(int id, client_connection *client_conn) {
        std::lock_guard<std::mutex> lk(mut);
        thread_que_map[id] = client_conn;
        fprintf(stdout, "comes a new user %s, here are %d users now.\n", client_conn->user_name, thread_que_map.size());
    }

    void erase(int id) {
        std::lock_guard<std::mutex> lk(mut);
        auto client_conn = thread_que_map[id];
        thread_que_map.erase(id);
        fprintf(stdout, "one user %s leaves, here are %d users now.\n", client_conn->user_name, thread_que_map.size());
    }

    client_connection *find(int id) {
        std::lock_guard<std::mutex> lk(mut);
        return thread_que_map[id];
    }

    // 用于在类外部进行遍历,满足基本可锁定 (BasicLockable)要求。
    void lock() { mut.lock(); }
    void unlock() { mut.unlock(); }

    ~msg_map() { fprintf(stdout, "msg_map has been deconstructed.\n"); }
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

class client_connection {
public:
    sockaddr_in address;
    msg_quene msg_que;
    char user_name[20];
    std::mutex mut;
    std::condition_variable cond;

    client_connection(int connfd, std::shared_ptr<msg_map> Msg_map) {}

    void push_back(std::shared_ptr<msg> Msg) {
        std::lock_guard<std::mutex> lk(mut);
        msg_que.push_back(Msg);
        cond.notify_one();
    }

    void process() {
        
    }
};