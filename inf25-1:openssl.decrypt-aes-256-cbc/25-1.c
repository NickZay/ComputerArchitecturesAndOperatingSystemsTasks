//Problem inf25-1: openssl/decrypt-aes-256-cbc
//Программе передается аргумент - пароль.
//
//На стандартый поток ввода подаются данные, зашифрованные алгоритмом AES-256-CBC с солью. Для получения начального вектора и ключа из пароля и соли используется алгоритм SHA-256.
//
//Необходимо расшифровать данные и вывести их на стандартый поток вывода.
//
//Используйте API OpenSSL/LibreSSL. Запуск сторонних команд через fork+exec запрещен.
//
//Отправляйте только исходный файл Си-программы с решением.


// echo {1..10000} > unsecure.txt
// openssl enc -aes-256-cbc -in unsecure.txt -out encrypted.txt -pass pass:password
// gcc 25-1.c `pkg-config openssl --cflags --libs` -o decipher
// cat encrypted.txt | ./decipher password

#include <openssl/evp.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define CHECK_ON_VALUE(WHAT_TO_CHECK, VALUE, ERROR) \
    if (WHAT_TO_CHECK == VALUE) {                   \
        perror(#ERROR);                             \
        exit(errno);                                \
    }

#define kMaxSize 256

int main(int argc, char** argv) {
    assert(argc == 2);
    const unsigned char* password = argv[1];

    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    CHECK_ON_VALUE(context, NULL, "new");

    unsigned char salt[8];
    CHECK_ON_VALUE(read(STDIN_FILENO, salt, 8), -1, "Salted__");
    CHECK_ON_VALUE(read(STDIN_FILENO, salt, 8), -1, "Salt");

    // For AES-256 the key size must be 256 bits or 32 bytes.
    // The old school modes such as CBC and CFB however simply require an IV of the same size as the block size.
    // The block size of AES is 16 bytes, whatever the key size.
    const int key_length = EVP_CIPHER_key_length(EVP_aes_256_cbc()); // == 32
    const int iv_length = EVP_CIPHER_iv_length(EVP_aes_256_cbc()); // == 16
    const int block_size = EVP_CIPHER_block_size(EVP_aes_256_cbc()); // == 16

    unsigned char* key = malloc(key_length * sizeof(unsigned char));
    unsigned char* iv = malloc(iv_length* sizeof(unsigned char));
    CHECK_ON_VALUE(key, NULL, "malloc");
    CHECK_ON_VALUE(iv, NULL, "malloc");

    // Генерация ключа и начального вектора из
    // пароля произвольной длины и 8-байтной соли
    EVP_BytesToKey(
        EVP_aes_256_cbc(),    // алгоритм шифрования
        EVP_sha256(),         // алгоритм хеширования пароля
        salt,                 // соль
        password, strlen(password), // пароль
        1,                    // количество итераций хеширования
        key,          // результат: ключ нужной длины
        iv            // результат: начальный вектор нужной длины
    );

    CHECK_ON_VALUE(EVP_DecryptInit_ex(context, EVP_aes_256_cbc(), NULL, key, iv),
                   0, "EVP_DecryptInit_ex");
    unsigned char ciphered_buffer[kMaxSize];
    // buffer out passed to EVP_DecryptUpdate() should have sufficient room for (inl + cipher_block_size) bytes
    unsigned char deciphered_buffer[kMaxSize + block_size];

    int read_result;
    int write_result;
    while ((read_result = read(STDIN_FILENO, ciphered_buffer, sizeof(ciphered_buffer))) > 0) {
        CHECK_ON_VALUE(EVP_DecryptUpdate(context, deciphered_buffer, &write_result, ciphered_buffer, read_result),
                       0, "EVP_DecryptUpdate");
        CHECK_ON_VALUE(write(STDOUT_FILENO, deciphered_buffer, write_result), -1, "write");
    }
    CHECK_ON_VALUE(read_result, -1, "read");
    CHECK_ON_VALUE(EVP_DecryptFinal_ex(context, deciphered_buffer, &write_result),
                   0, "EVP_DecryptFinal_ex");
    CHECK_ON_VALUE(write(STDOUT_FILENO, deciphered_buffer, write_result), -1, "write");

    EVP_CIPHER_CTX_free(context);
    free(key);
    free(iv);
    exit(EXIT_SUCCESS);
}