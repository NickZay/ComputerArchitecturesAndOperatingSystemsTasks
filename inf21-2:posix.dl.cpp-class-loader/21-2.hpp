//Problem inf21-2: posix/dl/cpp-class-loader
//Реализуйте механизм динамической загрузки классов C++ по аналогии с Java [https://docs.oracle.com/javase/7/docs/api/java/lang/ClassLoader.html].
//
//Необходимо реализовать функциональность классов ClassLoader и Class из предлагаемого интерфейса.
//
//Базовый каталог для поиска классов определен в переменной окружения CLASSPATH. Имя класса совпадает с каноническим именем файла библиотеки. Полное имя класса может содержать пространства имет C++, разделяемые символами ::. В этом случае необходимо искать файлы в соответствующих подкаталогах (по аналогии с пакетами в Java или Python).
//
//Пример: класс some::package::ClassInPackage будет находиться в библиотеке $CLASSPATH/some/package/ClassInPackage.so.
//
//Для загруженных классов необходимо уметь создавать их экземляры с помощью метода newInstance. При этом гарантируется, что каждый загружаемый класс имеет конструктор по умолчанию. В загружаемых классах могут быть виртуальные методы и виртуальные деструкторы.
//
//Интерфейс который необходимо реализовать:

#include "../../../../../usr/include/c++/9/algorithm" // for std::find
#include "../../../../../usr/include/c++/9/functional"
#include "../../../../../usr/include/c++/9/iostream"
#include "../../../../../usr/include/c++/9/memory"
#include "../../../../../usr/include/c++/9/string"
#include "../../../../../usr/include/c++/9/utility"
#include "../../../../../usr/include/dlfcn.h"
#include "../../../../../usr/include/unistd.h"

/*-----------------------------hpp--------------------------------------------*/

#include "../../../../../usr/include/c++/9/string"

class AbstractClass {
    friend class ClassLoader;
  public:
    explicit AbstractClass();
    ~AbstractClass();
  protected:
    void* newInstanceWithSize(size_t sizeofClass);
    struct ClassImpl* pImpl;
};

template <class T>
class Class : public AbstractClass {
  public:
    // one should free result
    T* newInstance()
    {
        size_t classSize = sizeof(T);
        void* rawPtr = newInstanceWithSize(classSize);
        return reinterpret_cast<T*>(rawPtr);
    }
};

enum class ClassLoaderError {
    NoError = 0,
    FileNotFound,
    LibraryLoadError,
    NoClassInLibrary
};


class ClassLoader {
  public:
    explicit ClassLoader();
    AbstractClass* loadClass(const std::string &fullyQualifiedName);
    ClassLoaderError lastError() const;
    ~ClassLoader();
  private:
    struct ClassLoaderImpl* pImpl;
};

/*--------------------------ClassLoaderError----------------------------------*/

std::ostream& operator<<(std::ostream& os, const ClassLoaderError& cle) {
#define IF_THEN_OS(WHAT) \
    if (cle == ClassLoaderError::WHAT) { \
        os << #WHAT; \
    }
    IF_THEN_OS(NoError);
    IF_THEN_OS(FileNotFound);
    IF_THEN_OS(LibraryLoadError);
    IF_THEN_OS(NoClassInLibrary);
    return os;
}

/*------------------------------ClassImpl-------------------------------------*/

struct ClassImpl {
  public:
    ClassImpl(void* handle, std::function<void*(void*)> constructor) :
          constructor(std::move(constructor)),
          handle(handle) { }
    ~ClassImpl() {
        if (handle != nullptr) {
            dlclose(handle);
        }
    }
    void* newInstanceWithSize(size_t sizeofClass) {
        void* place = malloc(sizeofClass);
        constructor(place);
        return place;
    }
  private:
    std::function<void*(void*)> constructor;
    void* handle;
};

/*---------------------------AbstractClass------------------------------------*/

AbstractClass::AbstractClass() : /* struct ClassImpl* */ pImpl(nullptr) { }

AbstractClass::~AbstractClass() {
    // struct ClassImpl*
    delete pImpl;
}

void* AbstractClass::newInstanceWithSize(size_t sizeofClass) {
    // struct ClassImpl*
    return pImpl->newInstanceWithSize(sizeofClass);
}

/*-----------------------------ClassLoaderImpl--------------------------------*/

// made it because of last_error
class ClassLoaderImpl {
  public:
    ClassLoaderImpl() : last_error(ClassLoaderError::NoError) { }
    [[nodiscard]] ClassLoaderError GetLastError() const {
        return last_error;
    }
    ClassImpl* LoadClassImpl(const std::string& fully_qualified_name) {
        last_error = ClassLoaderError::NoError;

        auto path = GetLibraryPath(fully_qualified_name);
        std::cout << path << std::endl;
        /* F_OK tests for the existence of the file. */
        if (access(path.data(), F_OK) == -1) {
            last_error = ClassLoaderError::FileNotFound;
            return nullptr;
        }
        /* The RTLD_NOW flag is the default; */
        void* library = dlopen(path.data(), RTLD_NOW);
        char* error = dlerror();
        if (error != nullptr) {
            fprintf(stderr, "%s\n", error);
            exit(EXIT_FAILURE);
        }
        if (library == nullptr) {
            last_error = ClassLoaderError::LibraryLoadError;
            return nullptr;
        }

        auto constructor_name = GetRealConstructorName(fully_qualified_name);
        std::function<void*(void*)> constructor =
            reinterpret_cast<void*(*)(void*)>(
                dlsym(library, constructor_name.data()));
        if (constructor == nullptr) {
            last_error = ClassLoaderError::NoClassInLibrary;
            dlclose(library);
            return nullptr;
        }
        return new ClassImpl(library, constructor);
    }
  private:
    static std::string GetLibraryPath(const std::string & fully_qualified_name) {
        std::string path = std::getenv("CLASSPATH");
        path += '/';
        auto current_begin = std::begin(fully_qualified_name);
        auto end = std::end(fully_qualified_name);
        while (current_begin != end) {
            auto current_end = std::find(current_begin, end, ':');
            path += {current_begin, current_end};
            if (current_end != end) {
                path += '/';
                current_end += 2;
            }
            current_begin = current_end;
        }
        return path + ".so";
    }
    static std::string GetRealConstructorName(const std::string & fully_qualified_name) {
        std::string constructor_name("_ZN");
        auto current_begin = std::begin(fully_qualified_name);
        auto end = std::end(fully_qualified_name);
        while (current_begin != end) {
            auto current_end = std::find(current_begin, end, ':');
            constructor_name += std::to_string(current_end - current_begin) + std::string{current_begin, current_end};
            if (current_end != end) {
                current_end += 2;
            }
            current_begin = current_end;
        }
        return constructor_name + "C1Ev";
    }
    ClassLoaderError last_error;
};

/*----------------------------ClassLoader-------------------------------------*/

ClassLoader::ClassLoader() : /* struct ClassLoaderImpl* */ pImpl(new ClassLoaderImpl()) { }

AbstractClass* ClassLoader::loadClass(std::string const& fullyQualifiedName) {
    auto abstract_class = new AbstractClass;
    // struct ClassLoaderImpl*
    abstract_class->pImpl = // struct ClassLoaderImpl*
        pImpl->LoadClassImpl(fullyQualifiedName);
    std::cout << lastError() << std::endl;
    return abstract_class;
}

[[nodiscard]] ClassLoaderError ClassLoader::lastError() const {
    // struct ClassLoaderImpl*
    return pImpl->GetLastError();
}

ClassLoader::~ClassLoader() {
    // struct ClassLoaderImpl*
    delete pImpl;
}

