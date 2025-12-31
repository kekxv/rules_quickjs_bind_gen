#include "my_api.h"
#include <iostream>

// ANSI 颜色码
#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"

std::string get_server_name() {
  return "Gemini Server";
}

int multiply(int a, int b) {
  return a * b;
}

void log_message(const std::string& msg) {
  std::cout << MAGENTA << "[LOG] " << msg << RESET << std::endl;
}

// 1. 接收结构体
void print_config(Config cfg) {
  std::cout << CYAN << "[C++] Received Config: "
            << "Host=" << cfg.host
            << ", Port=" << cfg.port
            << ", Debug=" << (cfg.debug_mode ? "ON" : "OFF")
            << RESET << std::endl;
}

// 2. 返回结构体
Config create_default_config() {
  Config c;
  c.host = "127.0.0.1";
  c.port = 80;
  c.debug_mode = false;
  return c;
}

// 3. 修改结构体指针
void update_user_score(User* user, int new_score) {
  if (user) {
    std::cout << YELLOW << "[C++] Updating user " << user->name
              << " score from " << user->score << " to " << new_score
              << RESET << std::endl;
    user->score = new_score;
  }
}

// 4. 工厂函数
User* create_user(const std::string& name, int id) {
  User* u = new User();
  u->name = name;
  u->id = id;
  u->score = 0;
  return u;
}