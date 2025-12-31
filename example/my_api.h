#pragma once

#include <string>
#include <iostream>

#define API_VERSION "3.1.4"
#define MAX_USERS 100

enum class SystemState {
  BOOTING = 0,
  READY,
  SHUTDOWN
};

// 简单的结构体
struct Config {
  int port;
  std::string host;
  bool debug_mode;
};

// 复杂结构体 (包含方法演示)
struct User {
  int id;
  std::string name;
  int score;
};

// --- 普通函数 ---
std::string get_server_name();

int multiply(int a, int b);

inline int add(int a, int b) {
  return a + b;
}

void log_message(const std::string& msg);

// --- [新增] 结构体交互演示 ---

// 1. 接收结构体 (按值传递，JS对象会被复制为C++对象)
void print_config(Config cfg);

// 2. 返回结构体 (返回给 JS 一个新对象)
Config create_default_config();

// 3. 修改传入的结构体 (按指针传递，JS对象的内部C++指针被修改)
void update_user_score(User* user, int new_score);

// 4. 工厂函数
User* create_user(const std::string& name, int id);