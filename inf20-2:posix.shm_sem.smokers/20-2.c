//Problem inf20-2: posix/shm_sem/smokers
//В баре сидят три курильщика - ценители самокруток из табака и бумаги. У каждого из них что-нибудь не хватает для полного счастья: первый (T) забыл дома табак, второй (P) - бумагу, третий (M) - спички. Видя страдания курильщиков, бармен периодически роется среди своих вещей, и что-нибудь находит, выдаваю недостающий предмет одному из курильщиков. Как только табак, бумага и спички у бармена заканчиваются, он всех выгоняет, разрешая докурить самокрутку тому, кто уже успех закурить.
//
//Смоделируйте эту задачу. Каждый персонаж - это отдельный процесс: три курильщика и бармен.
//
//На стандартный поток ввода подается последовательность латинских букв: t, p и m, - вещи, которые удается найти бармену, чтобы осчастливить одного из курильщиков. Как только последовательность букв заканчивается, то бармен всех выгоняет, отправляя им сигнал SIGTERM.
//
//Каждый курильщик, получив недостающий предмет, выкуривает самокрутку, записывая на стандартный поток вывода одну из букв латинского алфавита: T, P или M, - что ему не хватало. Если используется высокоуровневый ввод-вывод, не забывайте сбрасывать стандартный поток вывода.
//
//Бармен можен взаимодействовать одновременно только с одним курильщиком. Пока курильщики заняты своим делом, и не нуждаются в помощи, он отдыхает.
//
//Когда курильщики выкуривают самокрутку, они должны игнорировать просьбу бармена покинуть помещение, а моделирующие их процессы - завершить свою работу после того, как будет завершена операция вывода символа.
//
//Для реализации используйте семафоры POSIX, которые располагаются в общей для всех процессов памяти.

// gcc 20-2.c -lpthread -o smokers
// echo 'tpmmpttpmmpt' > a.txt
// cat a.txt | ./smokers

#include <semaphore.h>
#include <sys/mman.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>

#define CHECK_ON_ERROR(WHAT_TO_CHECK, ERROR) \
    if (WHAT_TO_CHECK == -1) {               \
        perror(#ERROR);                      \
        exit(errno);                         \
    }

#define kSmokersCount 3
int pids[kSmokersCount];

void SmokerFunction(sem_t* smoker_sem, sem_t* write_sem, char what_to_write) {
    while (1) {
        CHECK_ON_ERROR(sem_wait(smoker_sem), "sem_wait");
        CHECK_ON_ERROR(write(STDOUT_FILENO, &what_to_write, sizeof(what_to_write)), "write");
        CHECK_ON_ERROR(sem_post(write_sem), "sem_post");
    }
}

sem_t* OpenBar() {
    sem_t* sems = mmap(NULL, sizeof(sem_t) * 3 * kSmokersCount,
                       PROT_EXEC | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    CHECK_ON_ERROR((int64_t)sems, "mmap");

    for (int i = 0; i < 2 * kSmokersCount; ++i) {
        CHECK_ON_ERROR(sem_init(sems + i, 1, 0), "sem_init");
    }
    return sems;
}

void AcceptVisitors(sem_t* sems) {
    char letters[] = {'T', 'P', 'M'};
    for (int i = 0; i < kSmokersCount; ++i) {
        int fork_result = fork();
        CHECK_ON_ERROR(fork_result, "fork");
        if (fork_result == 0) {
            SmokerFunction(sems + i, sems + (i + kSmokersCount), letters[i]);
        } else {
            pids[i] = fork_result;
        }
    }
}

void BarmenFunction(sem_t* sems) {
    char what_is_come;
    while (read(STDIN_FILENO, &what_is_come, sizeof(what_is_come)) > 0) {
        #define IF_COME_THEN_NOTIFY(LETTER, SMOKER_NUMBER) \
            if (what_is_come == (LETTER)) { \
                CHECK_ON_ERROR(sem_post(sems + (SMOKER_NUMBER)), "sem_post");  \
                CHECK_ON_ERROR(sem_wait(sems + (SMOKER_NUMBER) + kSmokersCount), "sem_wait"); \
            }
        IF_COME_THEN_NOTIFY('t', 0);
        IF_COME_THEN_NOTIFY('p', 1);
        IF_COME_THEN_NOTIFY('m', 2);
    }
}

void CloseBar(sem_t* sems) {
    for (int i = 0; i < kSmokersCount; ++i) {
        CHECK_ON_ERROR(kill(pids[i], SIGTERM), "kill");
    }
    for (int i = 0; i < kSmokersCount; ++i) {
        CHECK_ON_ERROR(waitpid(pids[i], NULL, 0), "waitpid");
    }
    for (int i = 0; i < 2 * kSmokersCount; ++i) {
        CHECK_ON_ERROR(sem_destroy(sems + i), "sem_destroy")
    }
    CHECK_ON_ERROR(munmap(sems, sizeof(sem_t) * 2 * kSmokersCount), "munmap");
}

int main() {
    sem_t* sems = OpenBar();
    AcceptVisitors(sems);
    BarmenFunction(sems);
    CloseBar(sems);
    exit(EXIT_SUCCESS);
}