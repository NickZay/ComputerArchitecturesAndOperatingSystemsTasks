//Problem inf22-2: posix/sockets/udp-dns-resolver
//Программа читает со стандартного потока последовательность лексем - имен хостов.
//
//Необходимо для каждого имени сформировать UDP-запрос к DNS-серверу 8.8.8.8 для того, чтобы получить IP-адрес сервера для записи типа A. Далее - получить ответ от сервера и вывести IP-адрес на стандартный поток вывода.
//
//Гарантируется, что для каждого запроса существует ровно 1 IP-адрес.
//
//Указание: используйте инструменты dig и wireshark для того, чтобы исследовать формат запросов и ответов.
//
//Examples
//Input
//ejudge.ru
//ejudge.atp-fivt.org
//
//Output
//89.108.121.5
//87.251.82.74
//

// dig @8.8.8.8 ejudge.ru
// dig @8.8.8.8 ejudge.atp-fivt.org
// DNS primarily uses the User Datagram Protocol (UDP) on port number 53 to serve requests.
// https://habr.com/ru/post/478652/

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>


#define CHECK_ON_ERROR(WHAT_TO_CHECK, ERROR) \
    if (WHAT_TO_CHECK == -1) {               \
        perror(ERROR);                       \
        exit(errno);                         \
    }

#define MAX_SIZE 4096
// here can be an error with sizes
// The entire hostname, including the delimiting dots, has a maximum of 253 ASCII characters.
static uint8_t hostname[MAX_SIZE];
static uint8_t request[MAX_SIZE];
static uint8_t ipv4[MAX_SIZE];
#define BUFFER_SIZE MAX_SIZE
static uint8_t buffer[BUFFER_SIZE];

struct dns_header {
    // Данное поле используется как уникальный идентификатор транзакции.
    // Указывает на то, что пакет принадлежит одной и той же
    // сессии “запросов-ответов” и занимает 16 бит.
    uint16_t ID;
    // данный бит служит для индентификации того, является ли
    // пакет запросом (QR = 0) или ответом (QR = 1).
    //uint8_t QR : 1;
    // с помощью данного кода клиент может указать тип запроса, где обычное значение:
    //  0 — стандартный запрос,
    //  1 — инверсный запрос,
    //  2 — запрос статуса сервера.
    //  3-15 – зарезервированы на будущее.
    //uint8_t Opcode : 4;
    // данное поле имеет смысл только в DNS-ответах от сервера и
    // сообщает о том, является ли ответ авторитетным либо нет.
    //uint8_t AA : 1;
    // данный флаг устанавливается в пакете ответе в том случае
    // если сервер не смог поместить всю необходимую информацию
    // в пакет из-за существующих ограничений.
    //uint8_t TC : 1;
    // этот однобитовый флаг устанавливается в запросе и копируется в ответ.
    // Если он флаг устанавливается в запросе — это значит,
    // что клиент просит сервер не сообщать ему промежуточных ответов,
    // а вернуть только IP-адрес.
    //uint8_t RD : 1;
    uint8_t QR_Opcode_AA_TC_RD;
    // отправляется только в ответах, и сообщает о том, что сервер поддерживает рекурсию
    //uint8_t RA : 1;
    // являются зарезервированными и всегда равны нулю.
    //uint8_t Z : 3;
    // это поле служит для уведомления клиентов о том, успешно ли выполнен запрос или с ошибкой
    //  0 — значит запрос прошел без ошибок;
    //  1 — ошибка связана с тем, что сервер не смог понять форму запроса;
    //  2 — эта ошибка с некорректной работой сервера имен;
    //  3 — имя, которое разрешает клиент не существует в данном домене;
    //  4 — сервер не может выполнить запрос данного типа;
    //  5 — этот код означает, что сервер не может удовлетворить запроса клиента в силу административных ограничений безопасности.
    //uint8_t RCODE : 4;
    uint8_t RA_Z_RCODE;
    // QDCOUNT(16 бит) – количество записей в секции запросов
    // ANCOUNT(16 бит) – количество записей в секции ответы
    // NSCOUNT(16 бит) – количество записей в Authority Section
    // ARCOUNT(16 бит) – количество записей в Additional Record Section
    uint16_t QDCOUNT;
    uint16_t ANCOUNT;
    uint16_t NSCOUNT;
    uint16_t ARCOUNT;
} __attribute__((packed));

static struct dns_header dns_header;

uint64_t make_request() {
    uint8_t* current_host_begin = (uint8_t*)hostname;
    uint8_t* current_request_begin = request;
    for (int host_i = 0; hostname[host_i] != '\0'; ++host_i) {
        // если заканчивается на '.', то последнюю метку не ставим :)
        if (hostname[host_i] == '.') {
            continue;
        }
        if ((hostname[host_i + 1] == '.') || (hostname[host_i + 1] == '\0')) {
            uint8_t current_size = (hostname + host_i + 1) - current_host_begin;
            // обычная метка
            *(current_request_begin++) = 0b00111111 & current_size;
            memcpy(current_request_begin, current_host_begin, current_size);
            current_request_begin += current_size;
            *current_request_begin = '\0';

            current_host_begin = hostname + (host_i + 2);
        }
    }
    *(current_request_begin++) = '\0'; // 0b00111111 | 0;
    *current_request_begin = '\0'; // the end
    return current_request_begin - request;
}

struct dns_footer {
    uint16_t QTYPE; // QTYPE — Тип записи DNS, которую мы ищем (NS, A, TXT и т.д.).
    uint16_t QCLASS; // QCLASS — Определяющий класс запроса (IN для Internet).
} __attribute__((packed));

static struct dns_footer  dns_footer ;

void initialize_dns_header() {
    dns_header.ID = htons(0x9bce);
//    dns_header.QR = 0; // значит этот пакет является запросом;
//    dns_header.Opcode = 0b0000; // Стандартный запрос;
//    dns_header.AA = 0; // данное поле имеет смысл только в DNS-ответах, поэтому всегда 0;
//    dns_header.TC = 0; // данное поле имеет смысл только в DNS-ответах, поэтому всегда 0;
    dns_header.QR_Opcode_AA_TC_RD = 1; // Просим вернуть только IP адрес;
//    dns_header.RA = 0; // отправляется только сервером;
//    dns_header.Z = 0b000; // всегда нули, зарезервированное поле;
    dns_header.RA_Z_RCODE = 0; // Все прошло без ошибок
    dns_header.QDCOUNT = htons(0x0001); // 1 запись в секции запросов
    dns_header.ANCOUNT = htons(0x0000); // В запросе всегда 0, секция для ответов
    dns_header.NSCOUNT = htons(0x0000); // В запросе всегда 0, секция для ответов
    dns_header.ARCOUNT = htons(0x0000); // В запросе всегда 0, секция для ответов
}

void initialize_dns_footer() {
    dns_footer.QTYPE = htons(0x0001); // Соответствует типу A (запрос адреса хоста)
    dns_footer.QCLASS = htons(0x0001); // Соответствует классу IN

}

uint64_t combine_queries(uint64_t request_size) {
    memcpy(buffer, &dns_header, sizeof(dns_header));
    memcpy(buffer + sizeof(dns_header), request, request_size);
    memcpy(buffer + sizeof(dns_header) + request_size, &dns_footer, sizeof(dns_footer));
    return sizeof(dns_header) + request_size + sizeof(dns_footer);
}

void make_ipv4(int64_t received_size) {
    for (uint64_t i = 0, begin = received_size - 4; i < 4; ++i) {
        ipv4[i] = buffer[begin + i];
    }
    ipv4[5] = '\0';
}

void putc_ipv4() {
    for (int i = 0; i < 4; ++i) {
        printf("%d", ipv4[i]);
        if (i != 3) {
            putc('.', stdout);
        }
    }
}

void write_buffer(int64_t size) {
    for (int i = 0; i < size; ++i) {
        printf("%02X ", buffer[i]);
    }
    putc('\n', stdout);
}

// char и little endian это злоо
int main(int argc, char** argv) {
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    CHECK_ON_ERROR(socket_fd, "socket");
    const struct sockaddr_in addr_in = {
        .sin_family = AF_INET,
        .sin_port = htons(53), // UDP
        .sin_addr.s_addr = /* INADDR_LOOPBACK */ inet_addr("8.8.8.8")
    };
    while (scanf("%s", hostname) > 0) {
        initialize_dns_header();
        uint64_t request_size = make_request();
        initialize_dns_footer();

        uint64_t total_size = combine_queries(request_size);

//        write_buffer(total_size);
        CHECK_ON_ERROR(sendto(socket_fd, buffer, total_size, 0,
                              (const struct sockaddr*)&addr_in, sizeof(addr_in)), "sendto");
        int64_t received_size = recv(socket_fd, buffer, BUFFER_SIZE, 0);
        CHECK_ON_ERROR(received_size, "recv");
//        write_buffer(received_size);
        make_ipv4(received_size);
        putc_ipv4();
        putc('\n', stdout);
    }
    close(socket_fd);
    exit(EXIT_SUCCESS);
}