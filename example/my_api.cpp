#include "my_api.h"
#include <iostream>

// 实现普通函数
std::string get_server_name() {
  return "Gemini High-Performance Server";
}

// 实现多行声明的函数 (实现部分可以写成正常的单行)
int multiply(int a, int b) {
  std::cout << "C++: Multiplying " << a << " * " << b << std::endl;
  return a * b;
}

// 注意：add 函数是 inline 的，已经在 .h 中定义了函数体，
// 所以这里不需要（也不能）再次定义，否则会报重定义错误。

// 实现带注释声明的函数
void log_message(const std::string& msg) {
  std::cout << "[LOG]: " << msg << std::endl;
}