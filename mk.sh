g++ multi_thread_server.cpp threadpool.hpp connection.hpp broadcast.hpp -pthread -o multi_thread_server
g++ client.cpp -o client
g++ single_thread_server.cpp -o server