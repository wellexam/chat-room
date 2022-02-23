#include "threadpool.hpp"

#include "connection.hpp"
#include "broadcast.hpp"

#define USER_LIMIT 5                /*最大用户数量*/
const int MAX_FD = 65536;           //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数

int main(int argc, char *argv[]) {
    int port = 65500;
    if (argc == 2) {
        port = atoi(argv[1]);
    }

    auto listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int ret = 0; // 返回值

    sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    int flag = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    socklen_t len = sizeof(flag);
    getsockopt(listenfd, SOL_SOCKET, SO_SNDBUF, &flag, &len);
    std::cout << "TCP send buffer size is " << flag << "bytes" << std::endl;
    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(listenfd, 5);
    assert(ret >= 0);

    // setnonblocking(listenfd);

    pollfd fds[1];

    fds[0].fd = listenfd;
    fds[0].events = POLLIN | POLLOUT | POLL_ERR;

    auto Msg_map = std::make_shared<client_map>();

    auto pub_que = std::make_shared<msg_quene>();

    bool stop = false;

    threadpool<client_connection> thread_pool;

    try {
        // 广播线程
        std::thread(broadcast, Msg_map, pub_que, stop).detach();
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; }

    //事件循环
    while (!stop) {
        ret = poll(fds, 1, -1);
        if (ret < 1) {
            fprintf(stderr, "poll failure\n");
            break;
        }
        for (int i = 0; i < 1; i++) {
            if ((fds[i].fd == listenfd) && (fds[i].revents & POLLIN)) {
                // auto temp = std::make_shared<client_connection>(fds[i].fd, Msg_map);
                thread_pool.append(std::make_shared<client_connection>(fds[i].fd, Msg_map, pub_que));
            } else {
                stop = true;
                fprintf(stderr, "connection error\n");
            }
        }
    }

    stop = true;
    close(listenfd);
    return 0;
}