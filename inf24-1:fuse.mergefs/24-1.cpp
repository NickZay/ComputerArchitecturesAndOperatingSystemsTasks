//Problem inf24-1: fuse/mergefs
//Реализуйте файловую систему, доступную только для чтения, которая строит объединение нескольких каталогов в одно дерево, по аналогии с UnionFS, по следующим правилам:
//
//если встречаются два файла с одинаковым именем, то правильным считается тот, у которого более поздняя дата модификации;
//если встречаются два каталога с одинаковым именем, то нужно создавать объединение их содержимого.
//Внутри файловой системы могут быть только регулярные файлы с правами не выше 0444 и подкаталоги с правами не выше 0555.
//
//Программа для реализации файловой системы должна поддерживать стандартный набор опций FUSE и опцию для указания списка каталогов для объединения --src КАТАЛОГ_1:КАТАЛОГ_2:...:КАТАЛОГ_N.
//
//Используйте библиотеку FUSE версии 3.0 и выше. На сервер нужно отправить только исходный файл, который будет скомпилирован и слинкован с нужными опциями.

// dpkg -l | grep fuse
// pkg-config fuse3 --cflags --libs
// g++ -std=c++17 -Wall 24-1.cpp `pkg-config fuse3 --cflags --libs` -o myfs
// ./myfs work_dir -f --src fuse/a:fuse/b
// fusermount3 -u work_dir # if not -f

// ls   ==  readdir
// cat  ==  'open' and 'read'
// cd
// stat ==  getattr

// Тип off_t является знаковым, и по умолчанию 32-разрядным.
// Для того, чтобы уметь работать с файлами размером больше 2-х гигабайт,
// определяется значение переменной препроцессора до подключения заголовочных файлов:
#define FUSE_USE_VERSION 30 // API version 3.0
#define _FILE_OFFSET_BITS 64

//When applying permissions to directories on Linux, the permission bits have different meanings than on regular files.
//
//The read bit (r) allows the affected user to list the files within the directory
//The write bit (w) allows the affected user to create, rename, or delete files within the directory, and modify the directory's attributes
//The execute bit (x) allows the affected user to enter the directory, and access files and directories inside

// system_clock 's native precision (typically finer than milliseconds).

//#include <fuse3/fuse.h>
#include <algorithm>
#include <climits>
#include <filesystem>
#include <fuse.h>
#include <iostream>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace std {

template <>
struct hash<fs::path> {
    std::size_t operator()(const fs::path& path) const {
        return std::hash<std::string>{}(path.native());
    }
};

} // namespace std

namespace task {

namespace {

struct Options {
    char* directories_string;
};

Options option{};
std::vector<fs::path> full_directories_paths;
std::unordered_map<fs::path, std::vector<fs::path>> filesystem_tree;
char cwd[PATH_MAX];
std::string initial_working_directory;

} // namespace

auto FindLastChanged(const std::vector<fs::path>& paths) {
    fs::path result = *paths.begin();
    for (const auto& path : paths) {
        if (fs::last_write_time(result) < fs::last_write_time(path)) {
            result = path;
        }
    }
    return result;
}

// callback function to be called after 'stat' system call
int my_stat(const char* path, struct stat* st, struct fuse_file_info* fi) {
    const auto& it = filesystem_tree.find(path);
    if (it == filesystem_tree.end()) {
        return -ENOENT;
    }

    const auto& full_path = FindLastChanged(it->second);
    const auto& result = stat(full_path.c_str(), st);
    if (S_ISREG(st->st_mode)) {
        st->st_mode = S_IFREG | 0444;
    } else if (S_ISDIR(st->st_mode)) {
        st->st_mode = S_IFDIR | 0555;
    }

    return result;
}

// callback function to be called after 'readdir' system call
int my_readdir(
    const char* path,
    void* out,
    fuse_fill_dir_t filler,
    off_t off,
    struct fuse_file_info* fi,
    fuse_readdir_flags flags) {
    // filler(out, filename, stat, flags) -- заполняет информацию о файле и вставляет её в out
    // two mandatory entries: the directory itself and its parent
    filler(out, ".", nullptr, 0, fuse_fill_dir_flags(0));
    filler(out, "..", nullptr, 0, fuse_fill_dir_flags(0));

    int amount_of_directories = 0;
    const auto& it = filesystem_tree.find(path);
    if (it == filesystem_tree.end()) {
        return -ENOENT;
    }

    std::unordered_set<std::string> unique_filenames;
    for (const auto& full_path : it->second) {
        if (fs::is_directory(fs::status(full_path))) {
            ++amount_of_directories;
            // do not need recursive
            for (const auto& item : fs::directory_iterator(full_path)) {
                const std::string& item_name = item.path().filename().c_str();
                if (unique_filenames.find(item_name) ==
                    unique_filenames.end()) {
                    unique_filenames.insert(item_name);
                    filler(
                        out,
                        item_name.c_str(),
                        nullptr,
                        0,
                        fuse_fill_dir_flags(0));
                }
            }
        }
    }
    if (amount_of_directories == 0) {
        return -ENOENT;
    }
    return 0; // success
}

// callback function to be called after 'read' system call
int my_read(
    const char* path,
    char* out,
    size_t size,
    off_t off,
    struct fuse_file_info* fi) {
    const auto& it = filesystem_tree.find(path);
    if (it == filesystem_tree.end()) {
        return -ENOENT;
    }
    // открывать файловый дескриптор заранее или мапить заранее
    const auto& full_path = FindLastChanged(it->second);
    if (!fs::is_regular_file(fs::status(full_path))) {
        return -ENOENT;
    }

    if (off + size > fs::file_size(full_path)) {
        size = fs::file_size(full_path) - off;
    }
    int fd = open(full_path.c_str(), O_RDONLY, 0);
    lseek(fd, off, SEEK_SET);
    read(fd, out, size);
    close(fd);

    return size;
}

// register functions as callbacks
struct cpp_fuse_operations : fuse_operations {
    cpp_fuse_operations() : fuse_operations() {
        readdir = my_readdir;
        getattr = my_stat;
        read = my_read;
    }
} operations;

void SplitDirectories() {
    const std::string& directories_string = option.directories_string;
    auto current_begin = std::begin(directories_string);
    const auto& end = std::end(directories_string);

    while (current_begin != end) {
        const auto& current_end = std::find(current_begin, end, ':');
        if (*current_begin != '/') {
            full_directories_paths.emplace_back(
                initial_working_directory + '/' +
                std::string{current_begin, current_end});
        } else {
            full_directories_paths.emplace_back(current_begin, current_end);
        }
        current_begin = current_end;
        if (current_begin != end) {
            ++current_begin;
        }
    }
}

fs::path DeletePrefix(const fs::path& path, const fs::path& prefix) {
    return {std::string(path.c_str() + prefix.native().size())};
}

void MakeFilesystemTree() {
    for (const auto& full_directory_path : full_directories_paths) {
        for (const auto& full_path :
             fs::recursive_directory_iterator(full_directory_path)) {
            filesystem_tree[DeletePrefix(full_path, full_directory_path)]
                .push_back(full_path);
        }
        filesystem_tree["/"].push_back(full_directory_path / "");
    }
}

//void CoutFilesystem() {
//    for (auto [my_path, map] : filesystem_tree) {
//        std::cout << my_path;
////        std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> time_point;
////        time_point = std::chrono::time_point_cast<std::chrono::seconds>;
//        for (auto real_path : map) {
//            std::cout << "\n   " << real_path;
//        }
//        std::endl(std::cout);
//    }
//}

} // namespace task

int main(int argc, char* argv[]) {
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    const struct fuse_opt options_specifications[] = {
        {"--src %s", offsetof(task::Options, directories_string), 0},
        FUSE_OPT_END};

    // parse command line arguments, store matched by 'options_specifications'
    // options to 'option' value and remove them from {argc, argv}
    fuse_opt_parse(&args, &task::option, options_specifications, nullptr);

    getcwd(task::cwd, sizeof(task::cwd));
    task::initial_working_directory = task::cwd;

    task::SplitDirectories();
    task::MakeFilesystemTree();

    const int ret = fuse_main(args.argc, args.argv, &task::operations, nullptr);

    fuse_opt_free_args(&args);
    return ret;
}