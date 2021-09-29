// g++ main.cpp -ldl -o classloader
// export CLASSPATH=/home/nick/CLionProjects/CAOSextended
//
// Опция -fPIE компилятора указывает на то, что нужно сгенерировать
// позиционно-независимый код для main и _start, а опция -pie - о том,
// что нужно при линковке указать в ELF-файле, что он позиционно-независимый.
// Позиционно-независимый выполняемый файл в современных системах размещается по случайному адресу.
//
// Если позиционно-независимый исполняемый файл ещё и содержит таблицу экспортируемых символов,
// то он одновременно является и библиотекой.
// Если отсутствует опция -shared, то компилятор собирает программу,
// удаляя из неё таблицу символов. Явным образом сохранение таблицы символов задается опцией -Wl,-E.
//
// не работает:
// g++ -fPIE -pie -Wl,-E main.cpp -ldl -o SimpleClass.so
// работает:
// g++ -fPIC -shared main.cpp -o SimpleClass.so

#include "21-2.hpp"
#include <iostream>

class SimpleClass {
    int x = 3;
  public:
    SimpleClass() {
        std::cout << "Simple Class constructor called" << std::endl;
        ++x;
    }
};

static ClassLoader* Loader = nullptr;

int TestSimpleClass() {
//    std::cout << __LINE__ << std::endl;
    Class<SimpleClass>* simple_class_loader = reinterpret_cast<Class<SimpleClass>*>(Loader->loadClass("SimpleClass"));
//    std::cout << __LINE__ << std::endl;
    if (simple_class_loader) {
//        std::cout << __LINE__ << std::endl;
        SimpleClass* instance = simple_class_loader->newInstance(); // тут произошел аналог new SimpleClass()
//        std::cout << __LINE__ << std::endl;
        // над уничтожением объекта в этой задаче думать не нужно
        // но мы знаем устройство, поэтому уничтожим :)
        free(instance);
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}


int main() {
    SimpleClass(); // to make compiler understand that constructor is obligatory to compile
    Loader = new ClassLoader();
    int status = TestSimpleClass();
    delete Loader;
    return status;
}