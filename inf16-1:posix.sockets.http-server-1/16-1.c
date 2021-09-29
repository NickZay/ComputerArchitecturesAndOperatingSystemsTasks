//Problem inf16-1: posix/sockets/http-server-1
//Необходимо реализовать программу-сервер, которой передаются два аргумента: номер порта и полный путь к каталогу с данными.
//
//Программа должна прослушивать TCP-соединение на сервере localhost и указанным номером порта.
//
//После получения сигнала SIGTERM или SIGINT сервер должен закончить обработку текущего соединения, если оно есть, после чего корректно завершить свою работу.
//
//Внимание: в этой задаче признаком конца строк считается пара символов "\r\n", а не одиночный символ '\n'.
//
//Каждое соединение должно обрабатываться следующим образом:
//
//Клиент отправляет строку вида GET ИМЯ_ФАЙЛА HTTP/1.1
//Клиент отправляет произвольное количество непустых строк
//Клиент отправляет пустую строку
//После получения пустой строки от клиента, сервер должен отправить клиенту слеющие данные:
//
//Строку HTTP/1.1 200 OK, если файл существует, или HTTP/1.1 404 Not Found, если файл не существует, или HTTP/1.1 403 Forbidden, если файл существует, но не доступен для чтения
//Строку Content-Length: %d, где %d - размер файла в байтах
//Пустую строку
//Содержимое файла as-is
//После отправки ответа клиенту, нужно закрыть соединение и не нужно ждать ожидать от клиента следующих запросов.

// 500,000 TCP connections from a single server is the gold standard these days.
// The record is over a million.

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/sendfile.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK_OR_EXIT(WHAT_TO_CHECK, ERROR) \
    if (WHAT_TO_CHECK == -1) {              \
        perror(#ERROR);                     \
        exit(errno);                        \
    }

#define REAL_SIZE(STRING) sizeof(STRING) - 1

#define WRITE_AND_CHECK(STRING, ERROR)                          \
    const char result[] = STRING;                               \
    CHECK_OR_EXIT(write(accept_fd, result, REAL_SIZE(result)),  \
                ERROR);

static const int kMaxEventsToRead = 1;
static const int kSize = 4096;

int make_epoll() {
    const int epoll_fd = epoll_create1(0);
    CHECK_OR_EXIT(epoll_fd, "epoll_create1");
    return epoll_fd;
}

sigset_t make_sigset() {
    sigset_t sigset;
    sigemptyset(&sigset);

    sigaddset(&sigset, SIGTERM);
    sigaddset(&sigset, SIGINT);

    return sigset;
}

int make_signal_fd() {
    const sigset_t sigset = make_sigset();
    sigprocmask(/* how = */ SIG_SETMASK, &sigset, NULL);

    const int signal_fd = signalfd(-1, &sigset, 0);
    CHECK_OR_EXIT(signal_fd, "signalfd");
    return signal_fd;
}

void register_fd(const int epoll_fd, const int fd) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    event.events = EPOLLIN;
    CHECK_OR_EXIT(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event),
                  "epoll_ctl add fd");
}

int create_socket(const int port_number) {
    const int socket_fd = socket(/* domain = */ AF_INET, /* type = */ SOCK_STREAM, 0);
    CHECK_OR_EXIT(socket_fd, "socket");

    const struct sockaddr_in addr_in = {
        .sin_family = AF_INET,
        .sin_port = htons(port_number),
        .sin_addr.s_addr = /* INADDR_LOOPBACK */ inet_addr("127.0.0.1")
    };

    CHECK_OR_EXIT(bind(socket_fd, (struct sockaddr*)&addr_in, sizeof(struct sockaddr)),
                  "bind");
    CHECK_OR_EXIT(listen(socket_fd, SOMAXCONN), "listen");
    return socket_fd;
}

void main_loop(const int epoll_fd, const int signal_fd, const int socket_fd,
               const char* const path_to_directory) {
    struct epoll_event events[kMaxEventsToRead];
    char buffer[kSize + 1];
    char filename[FILENAME_MAX];
    char full_path[kSize + 1];

    while (1) {
        CHECK_OR_EXIT(epoll_wait(epoll_fd, events, kMaxEventsToRead, -1),
                      "epoll_wait");
        if (events[0].data.fd == signal_fd) {
            close(signal_fd);
            close(socket_fd);
            close(epoll_fd);
            exit(EXIT_SUCCESS);
        }
        if (events[0].data.fd == socket_fd) {
            const int accept_fd = accept(socket_fd, NULL, NULL);
            CHECK_OR_EXIT(accept_fd, "accept");
            register_fd(epoll_fd, accept_fd);
            continue;
        }

        const int accept_fd = events[0].data.fd;
        CHECK_OR_EXIT(fcntl(accept_fd, F_SETFD, fcntl(accept_fd, F_GETFL) | O_NONBLOCK),
                      "fcntl");

        const ssize_t amount_of_read = read(accept_fd, buffer, kSize);
        CHECK_OR_EXIT(amount_of_read, "read");

        buffer[amount_of_read] = '\0';
        sscanf(buffer, "GET %s HTTP/1.1\r\n", filename);
        // while (read(accept_fd, buffer, kSize) > 0) { }

        sprintf(full_path, "%s/%s", path_to_directory, filename);

        if (access(full_path, F_OK)) {
            WRITE_AND_CHECK("HTTP/1.1 404 Not Found\r\n", "write 404");
        }
        if (access(full_path, R_OK)) {
            WRITE_AND_CHECK("HTTP/1.1 403 Forbidden\r\n", "write 403");
        }

        int fd = open(full_path, 0);
        CHECK_OR_EXIT(fd, "open");

        struct stat stat;
        fstat(fd, &stat);

        WRITE_AND_CHECK("HTTP/1.1 200 OK\r\n", "write 200");
        dprintf(accept_fd, "Content-Length: %ld\r\n\r\n", stat.st_size);
        sendfile(accept_fd, fd, NULL, stat.st_size);

        close(fd);
        shutdown(accept_fd, 0);
        CHECK_OR_EXIT(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL),
                      "epoll_ctl del fd");
    }
}

int main(int argc, char** argv) {
    assert(argc == 3);

    int port_number;
    sscanf(argv[1], "%d", &port_number);

    const char* const path_to_directory = argv[2];

    const int epoll_fd = make_epoll();

    const int signal_fd = make_signal_fd();
    register_fd(epoll_fd, signal_fd);

    const int socket_fd = create_socket(port_number);
    register_fd(epoll_fd, socket_fd);

    main_loop(epoll_fd, signal_fd, socket_fd, path_to_directory);
}