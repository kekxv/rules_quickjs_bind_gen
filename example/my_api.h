#pragma once

#include <string>

// --- 1. 宏定义 (保持不变) ---
#define API_VERSION "3.1.4"
#define MAX_USERS 100

// --- 2. 枚举 (保持不变) ---
enum class SystemState
{
  BOOTING = 0,
  READY,
  SHUTDOWN
};

// --- 3. 普通函数 (干净的声明) ---
std::string get_server_name();

// --- 4. 关键测试：多行声明 ---
// 生成器现在可以处理这种“丑陋”但合法的 C++ 格式
// 返回类型在第一行，函数名在第二行，参数列表跨越多行
int
multiply(int a,
         int b);

// --- 5. 关键测试：Inline 函数 (带函数体) ---
// 生成器会识别到 '{' 并停止，自动忽略 inline 关键字
inline int add(int a, int b)
{
  return a + b;
}

// --- 6. 关键测试：带注释 ---
// 生成器会先剔除注释，所以这行是安全的
void log_message(const std::string& msg); // 这是一个尾随注释
