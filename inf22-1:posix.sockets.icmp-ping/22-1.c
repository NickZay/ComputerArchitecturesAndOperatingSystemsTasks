//Problem inf22-1: posix/sockets/icmp-ping
//Программа принимает три аргумента: строку с IPv4-адресом, и два неотрицательных целых числа, первое из которых определяет общее время работы программы timeout, а второе - время между отдельными запросами в микросекундах interval.
//
//Необходимо реализовать упрощённый аналог утилиты ping, которая определяет доступность удаленного хоста, используя протокол ICMP.
//
//Программа должна последовательно отправлять echo-запросы к указанному адресу и подсчитывать количество успешных ответов. Между запросами, во избежание большой нагрузки на сеть, необходимо выдерживать паузу в interval микросекунд (для этого можно использовать функцию usleep).
//
//Через timeout секунд необходимо завершить работу, и вывести на стандартный поток вывода количество полученных ICMP-ответов, соответствующих запросам.
//
//В качестве аналога можно посмотреть утилиту /usr/bin/ping.
//
//Указания: используйте инструменты ping и wireshark для того, чтобы исследовать формат запросов и ответов. Для того, чтобы выполняемый файл мог без прав администратора взаимодействовать с сетевым интерфейсом, нужно после компиляции установить ему capabilities командой: setcap cat_net_raw+eip PROGRAM. Контрольная сумма для ICMP-заголовков вычисляется по алгоритму из RFC-1071.



//    struct timespec {
//        time_t tv_sec;    // время в секундах
//        long tv_nsec;     // доля времени в наносекундах
//    };
//
//    int clock_gettime(clockid_t id, struct timespec *tp);
//
//    CLOCK_REAL - значение астрономического времени, где за точку отсчета принимается начало эпохи - 1 января 1970 года;
//    CLOCK_MONOTONIC - значение времени с момента загрузки ядра, исключая то время, пока система находилась в спящем режиме;
//    CLOCK_PROCESS_CPUTIME_ID - значение времени, затраченного на выполнение текущего процесса;
//    CLOCK_THREAD_CPUTIME_ID - значение времени, затраченного на выполнение текущего потока.

// CAP_NET_RAW
// use RAW and PACKET sockets;
// bind to any address for transparent proxying.
//
//p - (Permitted) - полномочие разрешено для исполняемого файла;
//i - (Inherited) - может наследоваться при вызове exec, но это не распространяется при создании дочернего процесса через fork ;
//e - (Effective) - набор полномочий добавляется в существующее множество разрешений, а не заменяет его.
//
// Для создания таких сокетов требуются либо права root,
// либо настройка cap_net_raw, в противном случае
// системный вызов socket вернет значение -1.
// gcc 22-1.c -o ping; sudo setcap cap_net_raw,cap_net_admin+eip ./ping; ./ping 8.8.8.8 4 10000

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/ip_icmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <time.h>
#include <unistd.h>

#define CHECK_OR_EXIT(WHAT_TO_CHECK, ERROR) \
    if (WHAT_TO_CHECK == -1) {              \
        perror(#ERROR);                     \
        exit(errno);                        \
    }

//Automatic port number
#define PORT_NUMBER 0
#define PING_PACKET_SIZE 64

struct ping_packet {
    struct icmphdr header;
    char message[PING_PACKET_SIZE - sizeof(struct icmphdr)];
} __attribute__((packed));

uint16_t RFC_1071(void* data_ptr, int size) {
    uint16_t* buf = data_ptr;
    uint32_t sum = 0;

    for (; size > 1; size -= 2) {
        sum += *buf;
        ++buf;
    }
    if (size == 1) {
        sum += *(u_char*) buf;
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return ~sum;
}

struct sockaddr_in do_dns_business(const char* const ipv4) {
    struct hostent* host_entity = gethostbyname(ipv4);
    struct sockaddr_in addr_in;
    struct in_addr in_addr;
    addr_in = (struct sockaddr_in) {
        .sin_family = host_entity->h_addrtype,
        .sin_port = htons(PORT_NUMBER),
        //.sin_addr.s_addr = in_addr.s_addr
        .sin_addr.s_addr = *(long*)host_entity->h_addr
    };
    return addr_in;
}

unsigned int find_usleep_time(const int interval, const struct timespec start, const struct timespec end) {
    int value = interval - ((int)end.tv_nsec - (int)start.tv_nsec) / 1000;
    return value < 0 ? 0 : value;
}

void fill_message(char message[], int size) {
    //random packet message
    for (int i = 0; i < size - 1; ++i) {
        message[i] = (char)i + '0';
    }
    message[size - 1] = '\0';
}

bool is_time_ended(const int timeout, struct timespec start_time, struct timespec current_time) {
    return ((current_time.tv_sec - start_time.tv_sec) > timeout
        || ((current_time.tv_sec - start_time.tv_sec) == timeout
            && current_time.tv_nsec >= start_time.tv_nsec));
}

int main_loop(const char* const ipv4, const int interval, const int timeout) {
    const struct sockaddr_in addr_in = do_dns_business(ipv4);

    const int icmp_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    CHECK_OR_EXIT(icmp_fd, "socket");

    struct timeval tv_out;
    tv_out.tv_sec = 1;
    tv_out.tv_usec = 0;
    setsockopt(icmp_fd, SOL_SOCKET, SO_RCVTIMEO, &tv_out, sizeof(tv_out));

    struct ping_packet packet;
    int message_count = 0;
    struct timespec start_iteration_time, end_iteration_time;
    struct timespec start_time, current_time;

    // Monotonic time is useful for measuring elapsed times, because
    // it guarantees that those measurements are not affected by changes to the system clock
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    while (!is_time_ended(timeout, start_time, current_time)) {
        clock_gettime(CLOCK_MONOTONIC, &start_iteration_time);

        memset(&packet, 0, sizeof(packet));
        packet.header.type = ICMP_ECHO;                     /* Type = 8(IPv4, ICMP) 128(IPv6,ICMP6) */
        packet.header.code = 0;                             /* Code = 0 */
        packet.header.un.echo.sequence = message_count++;   /* message_number */
        packet.header.un.echo.id = getpid();
        fill_message(packet.message, sizeof(packet.message));

        packet.header.checksum = RFC_1071(&packet, sizeof(packet));

        clock_gettime(CLOCK_MONOTONIC, &end_iteration_time);
        CHECK_OR_EXIT(usleep(interval/*find_usleep_time(interval, start_iteration_time, end_iteration_time)*/),
                      "usleep");

        CHECK_OR_EXIT(sendto(icmp_fd, &packet, sizeof(packet), 0,
                (const struct sockaddr*)&addr_in, sizeof(addr_in)), "sendto");
        //If no messages are available at the socket, the receive calls wait for a
        //message to arrive, unless the socket is nonblocking (see fcntl(2))
        CHECK_OR_EXIT(recv(icmp_fd, &packet, sizeof(packet), 0),
                      "recv");

        clock_gettime(CLOCK_MONOTONIC, &current_time);
    }
    return message_count;
}

int main(int argc, char** argv) {
    assert(argc == 4);
    const char* const ipv4 = argv[1];

    int timeout, interval;
    sscanf(argv[2], "%d", &timeout);
    sscanf(argv[3], "%d", &interval);

    int amount_of_received = main_loop(ipv4, interval, timeout);

    printf("%d\n", amount_of_received);
    exit(EXIT_SUCCESS);
}