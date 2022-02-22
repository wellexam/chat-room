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

#include "connection.hpp"

void traverse(std::shared_ptr<msg_map> Msg_map, std::shared_ptr<msg> Msg) {
    std::lock_guard<msg_map> lk(*Msg_map);
    for (auto &i : Msg_map->thread_que_map) {
        if (Msg->user_id == i.first) {
            continue;
        }
        i.second->push_back(Msg);
    }
}

void broadcast(std::shared_ptr<msg_map> Msg_map, std::shared_ptr<public_msg_que> pub_que, bool stop) {
    while (!stop) {
        auto Msg = pub_que->pop();
        traverse(Msg_map, Msg);
    }
}