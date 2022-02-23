#pragma once

#include "connection.hpp"

void traverse(std::shared_ptr<client_map> Msg_map, std::shared_ptr<msg> Msg) {
    std::lock_guard<client_map> lk(*Msg_map);
    for (auto &i : Msg_map->thread_que_map) {
        if (Msg->user_id == i.first) {
            continue;
        }
        i.second->push_back(Msg);
    }
}

void broadcast(std::shared_ptr<client_map> Msg_map, std::shared_ptr<msg_quene> pub_que, bool stop) {
    while (!stop) {
        auto Msg = pub_que->pop();
        traverse(Msg_map, Msg);
    }
}