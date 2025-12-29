#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <algorithm> // added for cleanup
#include <boost/filesystem.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>

namespace fs = boost::filesystem;

// 宏保护状态结构
struct GuardState
{
  std::string line; // 完整的 #if/ifdef 行 (例如 "#if LV_USE_BIDI")
  std::string symbol; // 如果是 #ifndef X，这里存储 X，用于检测 Header Guard
  bool isHeaderGuard; // 标记是否为头文件保护符
};

struct FuncDef
{
  std::string retType, name, args;
  std::vector<std::string> guards;
};

struct EnumDef
{
  std::string name;
  std::vector<std::pair<std::string, int>> members;
  std::vector<std::string> guards;
};

struct MacroDef
{
  std::string name, value;
};

class BindingGenerator
{
  std::string inputPath;
  std::string outputDir;
  std::string moduleName;
  std::vector<std::string> extraIncludes;

  std::vector<FuncDef> functions;
  std::vector<EnumDef> enums;
  std::vector<MacroDef> macros;

public:
  BindingGenerator(std::string in, std::string out, std::string mod, std::vector<std::string> extras)
    : inputPath(in), outputDir(out), moduleName(mod), extraIncludes(extras)
  {
  }

  std::string remove_comments(const std::string& source)
  {
    try
    {
      boost::regex re_block(R"(/\*[\s\S]*?\*/)");
      std::string temp = boost::regex_replace(source, re_block, "");
      boost::regex re_line(R"(//[^\r\n]*)");
      std::string clean = boost::regex_replace(temp, re_line, "");
      if (clean.length() < 10 && source.length() > 50) return source;
      return clean;
    }
    catch (...)
    {
      return source;
    }
  }

  std::string clean_type_string(std::string raw)
  {
    boost::regex re_space(R"([\r\n\t]+)");
    std::string s = boost::regex_replace(raw, re_space, " ");
    boost::regex re_keywords(R"(\b(inline|static|constexpr|extern|virtual|explicit)\b)");
    s = boost::regex_replace(s, re_keywords, "");
    boost::regex re_multi_space(R"(\s+)");
    s = boost::regex_replace(s, re_multi_space, " ");
    s = boost::regex_replace(s, boost::regex(R"(^\s+|\s+$)"), "");
    return s;
  }

  std::string clean_args_string(std::string raw)
  {
    boost::regex re_space(R"([\r\n\t]+)");
    return boost::regex_replace(raw, re_space, " ");
  }

  // [New] C++ 类型到 TypeScript 类型的简单映射
  std::string cpp_to_ts_type(std::string cppType)
  {
    // 移除 const, volatile, & 等修饰符以便匹配
    std::string t = cppType;
    boost::replace_all(t, "const", "");
    boost::replace_all(t, "volatile", "");
    boost::replace_all(t, "&", "");
    boost::trim(t);

    if (t == "void") return "void";
    if (t == "bool") return "boolean";

    // 字符串
    if (t.find("char*") != std::string::npos || t.find("char *") != std::string::npos ||
      t.find("std::string") != std::string::npos)
    {
      return "string";
    }

    // 数字类型
    static const std::set<std::string> numTypes = {
      "int", "short", "long", "float", "double", "size_t",
      "uint8_t", "int8_t", "uint16_t", "int16_t",
      "uint32_t", "int32_t", "uint64_t", "int64_t", "unsigned int"
    };

    // 检查是否包含数字关键字
    bool isNum = false;
    for (const auto& nt : numTypes)
    {
      if (t.find(nt) != std::string::npos && t.find("*") == std::string::npos)
      {
        isNum = true;
        break;
      }
    }
    if (isNum) return "number";

    // 如果是指针，通常映射为 any 或者 number (作为句柄)，这里暂用 any 安全
    // 如果是已知的枚举，可以映射为枚举名（这里做一个简单的查找优化）
    for (const auto& e : enums)
    {
      if (t.find(e.name) != std::string::npos) return e.name;
    }

    return "any";
  }

  // [Fixed] 解析参数字符串并生成 TS 签名 (arg1: type, arg2: type)
  std::string format_ts_args(const std::string& rawArgs)
  {
    if (rawArgs.empty() || rawArgs == "void") return "";

    std::vector<std::string> args;
    boost::split(args, rawArgs, boost::is_any_of(","));

    std::stringstream ss;
    int argCount = 0;

    for (size_t i = 0; i < args.size(); ++i)
    {
      std::string argStr = args[i];
      boost::trim(argStr);
      if (argStr.empty()) continue;
      if (argStr == "void") continue;

      std::string type, name;
      bool isArray = false; // [Fix] 标记是否为数组

      // 匹配结尾的变量名，识别 name[] 这种写法
      boost::regex re_arg_name(R"((\w+)(\[\])?$)");
      boost::smatch m;

      if (boost::regex_search(argStr, m, re_arg_name))
      {
        std::string possibleName = m[1]; // 纯名字，不带 []
        if (m[2].matched) isArray = true; // [Fix] 捕获到了 []

        std::string rest = m.prefix();
        boost::trim(rest);

        if (rest.empty() || rest == "const" || rest == "unsigned")
        {
          // 只有类型的情况，比如 "int[]" 或 "int"
          type = argStr;
          name = "arg" + std::to_string(argCount++);
        }
        else
        {
          name = possibleName;
          type = rest;
          argCount++;
        }
      }
      else
      {
        type = argStr;
        name = "arg" + std::to_string(argCount++);
      }

      // [Fix] 生成 TS 类型
      std::string tsType = cpp_to_ts_type(type);
      if (isArray)
      {
        tsType += "[]";
      }

      if (i > 0) ss << ", ";
      ss << name << ": " << tsType;
    }
    return ss.str();
  }

  std::vector<std::string> get_active_guards(const std::vector<GuardState>& stack)
  {
    std::vector<std::string> active;
    for (const auto& g : stack)
    {
      if (!g.isHeaderGuard)
      {
        active.push_back(g.line);
      }
    }
    return active;
  }

  std::string invert_guard(const std::string& line)
  {
    boost::regex re_ifdef(R"(^\s*#ifdef\s+(.*))");
    boost::smatch m;
    if (boost::regex_match(line, m, re_ifdef))
    {
      return "#ifndef " + m[1].str();
    }
    boost::regex re_ifndef(R"(^\s*#ifndef\s+(.*))");
    if (boost::regex_match(line, m, re_ifndef))
    {
      return "#ifdef " + m[1].str();
    }
    boost::regex re_if(R"(^\s*#if\s+(.*))");
    if (boost::regex_match(line, m, re_if))
    {
      std::string cond = m[1].str();
      boost::trim(cond);
      return "#if !(" + cond + ")";
    }
    return line;
  }

  void parse()
  {
    std::ifstream file(inputPath);
    if (!file.is_open())
    {
      std::cerr << "Error: Cannot open file " << inputPath << std::endl;
      exit(1);
    }

    std::string line;
    std::vector<GuardState> guardStack;
    std::string buffer;

    boost::regex re_macro_val(R"(^\s*#define\s+([A-Z0-9_]+)\s+(\".*\"|-?\d+(\.\d+)?))");
    boost::regex re_ifndef(R"(^\s*#ifndef\s+([A-Z0-9_]+))");
    boost::regex re_define_simple(R"(^\s*#define\s+([A-Z0-9_]+))");
    boost::regex re_elif(R"(^\s*#elif\s+(.*))");
    boost::regex re_enum_cpp(R"(enum\s+(class\s+)?(\w+)\s*\{([^}]*)\};)");
    boost::regex re_enum_c(R"(typedef\s+enum\s*\{([^}]*)\}\s*(\w+);)");
    boost::regex re_func(R"(([a-zA-Z0-9_:<>\*&\s]+?)\s+(\w+)\s*\(([\s\S]*?)\)\s*(?:;|{))");
    boost::regex re_enum_member(R"((\w+)(\s*=\s*(-?\d+))?)");

    std::set<std::string> blacklist = {
      "if", "while", "for", "switch", "return", "sizeof", "catch", "else",
      "using", "template", "operator", "void"
    };

    bool in_comment_block = false;

    while (std::getline(file, line))
    {
      std::string trimmed = line;
      boost::trim(trimmed);

      if (in_comment_block)
      {
        if (trimmed.find("*/") != std::string::npos) in_comment_block = false;
        continue;
      }
      if (trimmed.find("/*") != std::string::npos)
      {
        if (trimmed.find("*/") == std::string::npos) in_comment_block = true;
        trimmed = trimmed.substr(0, trimmed.find("/*"));
      }
      if (trimmed.find("//") != std::string::npos)
      {
        trimmed = trimmed.substr(0, trimmed.find("//"));
        boost::trim(trimmed);
      }
      if (trimmed.empty()) continue;

      if (trimmed[0] == '#')
      {
        boost::smatch m;
        if (boost::starts_with(trimmed, "#define"))
        {
          if (boost::regex_search(trimmed, m, re_define_simple))
          {
            std::string defSym = m[1];
            if (!guardStack.empty())
            {
              if (guardStack.back().symbol == defSym)
              {
                guardStack.back().isHeaderGuard = true;
                continue;
              }
            }
          }
          if (boost::regex_search(trimmed, m, re_macro_val))
          {
            macros.push_back({m[1], m[2]});
          }
        }
        else if (boost::starts_with(trimmed, "#if"))
        {
          GuardState gs;
          gs.line = trimmed;
          gs.isHeaderGuard = false;
          if (boost::starts_with(trimmed, "#ifndef") && boost::regex_search(trimmed, m, re_ifndef))
          {
            gs.symbol = m[1];
          }
          guardStack.push_back(gs);
        }
        else if (boost::starts_with(trimmed, "#endif"))
        {
          if (!guardStack.empty()) guardStack.pop_back();
        }
        else if (boost::starts_with(trimmed, "#else"))
        {
          if (!guardStack.empty())
          {
            std::string prev = guardStack.back().line;
            guardStack.pop_back();
            guardStack.push_back({invert_guard(prev), "", false});
          }
        }
        else if (boost::starts_with(trimmed, "#elif"))
        {
          if (!guardStack.empty())
          {
            guardStack.pop_back();
            if (boost::regex_search(trimmed, m, re_elif))
            {
              std::string cond = m[1];
              guardStack.push_back({"#if " + cond, "", false});
            }
          }
        }
        continue;
      }

      buffer += trimmed + " ";

      bool is_stmt_end = (trimmed.back() == ';' || trimmed.back() == '}');
      bool is_block_start = (trimmed.back() == '{');

      if (is_stmt_end || is_block_start)
      {
        boost::smatch m;

        if (buffer.find("typedef") != std::string::npos && buffer.find("enum") == std::string::npos)
        {
          if (is_stmt_end) buffer.clear();
          continue;
        }

        if (is_stmt_end)
        {
          if (boost::regex_search(buffer, m, re_enum_cpp))
          {
            process_enum(m[2], m[3], get_active_guards(guardStack), re_enum_member);
            buffer.clear();
            continue;
          }
          if (boost::regex_search(buffer, m, re_enum_c))
          {
            process_enum(m[2], m[1], get_active_guards(guardStack), re_enum_member);
            buffer.clear();
            continue;
          }
        }

        if (boost::regex_search(buffer, m, re_func))
        {
          std::string rawRet = m[1];
          std::string name = m[2];
          std::string rawArgs = m[3];

          if (rawArgs.find("...") != std::string::npos)
          {
            buffer.clear();
            continue;
          }

          if (blacklist.count(name) == 0 && rawRet.find('#') == std::string::npos)
          {
            std::string cleanRet = clean_type_string(rawRet);
            if (!cleanRet.empty())
            {
              functions.push_back({cleanRet, name, clean_args_string(rawArgs), get_active_guards(guardStack)});
              buffer.clear();
              continue;
            }
          }
        }

        if (is_stmt_end)
        {
          buffer.clear();
        }
      }
    }
  }

  void process_enum(std::string name, std::string body, const std::vector<std::string>& guards,
                    const boost::regex& /*ignored*/)
  {
    EnumDef edef;
    edef.name = name;
    edef.guards = guards;

    // 符号表：记录当前枚举已解析成员的值
    std::map<std::string, int> symbol_table;

    // 升级版正则：支持 十进制、十六进制、别名 (标识符)
    // Group 1: Key
    // Group 2: Value String (可能是 123, -1, 0xFF, 或 ANOTHER_KEY)
    boost::regex re_member(R"(([a-zA-Z0-9_]+)\s*(?:=\s*([a-zA-Z0-9_]+|-?\d+|0x[0-9a-fA-F]+))?)");

    boost::smatch m;
    int currentVal = 0;

    auto start = body.cbegin();
    while (boost::regex_search(start, body.cend(), m, re_member))
    {
      std::string key = m[1];
      std::string val_str = m[2];

      // 过滤掉可能是数字开头的误判 (虽然 regex [a-z] 限制了，但防万一)
      if (isdigit(key[0]))
      {
        start = m.suffix().first;
        continue;
      }

      int val = currentVal;

      // 如果有显式赋值
      if (m[2].matched && !val_str.empty())
      {
        // 1. 十六进制 (0x...)
        if (val_str.find("0x") == 0 || val_str.find("0X") == 0)
        {
          try { val = std::stoul(val_str, nullptr, 16); }
          catch (...)
          {
          }
        }
        // 2. 十进制数字 (123, -123)
        else if (isdigit(val_str[0]) || val_str[0] == '-')
        {
          try { val = std::stoi(val_str); }
          catch (...)
          {
          }
        }
        // 3. 别名 (ANOTHER_KEY)
        else
        {
          if (symbol_table.count(val_str))
          {
            val = symbol_table[val_str];
          }
          else
          {
            // 如果引用了未定义的宏或者向前引用，这里没法完美解析
            // 为了不报错，我们保持 currentVal 不变，或者打印警告
            // std::cerr << "[Gen Warning] Enum alias '" << val_str << "' not found." << std::endl;
          }
        }
        // 更新基准值
        currentVal = val;
      }

      // 记录到符号表
      symbol_table[key] = val;
      edef.members.push_back({key, val});

      // 下一个值默认 +1
      currentVal++;

      start = m.suffix().first;
    }
    enums.push_back(edef);
  }

  void generate()
  {
    fs::path outCppPath = fs::path(outputDir) / (moduleName + "_bind.cpp");
    fs::path outHPath = fs::path(outputDir) / (moduleName + "_bind.h");
    fs::path outTSPath = fs::path(outputDir) / (moduleName + ".d.ts"); // [New] TS path

    // 1. Generate C++ Binding Code
    std::ofstream out(outCppPath.string());
    out << "// Generated by Project Gemini\n";
    out << "#include \"quickjs.h\"\n";
    out << "#include \"qjs_utils.hpp\"\n";

    for (const auto& inc : extraIncludes)
    {
      if (inc.empty()) continue;
      if (inc.find('<') == std::string::npos && inc.find('"') == std::string::npos)
      {
        out << "#include \"" << inc << "\"\n";
      }
      else
      {
        out << "#include " << inc << "\n";
      }
    }

    out << "\nstatic const JSCFunctionListEntry js_" << moduleName << "_funcs[] = {\n";

    for (const auto& f : functions)
    {
      for (const auto& g : f.guards) out << g << "\n";
      out << "    JS_CFUNC_DEF(\"" << f.name << "\", 0, (Wrapper<" << f.name << ">::call)),\n";
      for (size_t i = 0; i < f.guards.size(); ++i) out << "#endif\n";
    }

    for (const auto& m : macros)
    {
      if (m.value.find('"') != std::string::npos)
      {
        out << "    JS_PROP_STRING_DEF(\"" << m.name << "\", " << m.value << ", JS_PROP_CONFIGURABLE),\n";
      }
      else
      {
        out << "    JS_PROP_DOUBLE_DEF(\"" << m.name << "\", " << m.value << ", JS_PROP_CONFIGURABLE),\n";
      }
    }
    out << "};\n\n";

    out << "extern \"C\" JSModuleDef* js_init_module_" << moduleName << "(JSContext* ctx, const char* module_name) {\n";
    out << "    JSModuleDef* m = JS_NewCModule(ctx, module_name, [](JSContext* ctx, JSModuleDef* m) {\n";
    out << "        if (JS_SetModuleExportList(ctx, m, js_" << moduleName << "_funcs, \n";
    out << "                                   sizeof(js_" << moduleName <<
      "_funcs)/sizeof(JSCFunctionListEntry)) != 0) return -1;\n";

    for (const auto& e : enums)
    {
      for (const auto& g : e.guards) out << "    " << g << "\n";
      out << "        {\n";
      out << "            JSValue enum_obj = JS_NewObject(ctx);\n";
      for (const auto& mem : e.members)
      {
        out << "            JS_SetPropertyStr(ctx, enum_obj, \"" << mem.first << "\", JS_NewInt32(ctx, " << mem.second
          << "));\n";
      }
      out << "            JS_SetModuleExport(ctx, m, \"" << e.name << "\", enum_obj);\n";
      out << "        }\n";
      for (size_t i = 0; i < e.guards.size(); ++i) out << "    #endif\n";
    }
    out << "        return 0;\n";
    out << "    });\n\n";
    out << "    if (!m) return nullptr;\n\n";
    out << "    JS_AddModuleExportList(ctx, m, js_" << moduleName << "_funcs, sizeof(js_" << moduleName <<
      "_funcs)/sizeof(JSCFunctionListEntry));\n";

    for (const auto& e : enums)
    {
      for (const auto& g : e.guards) out << "    " << g << "\n";
      out << "    JS_AddModuleExport(ctx, m, \"" << e.name << "\");\n";
      for (size_t i = 0; i < e.guards.size(); ++i) out << "    #endif\n";
    }
    out << "    return m;\n";
    out << "}\n";
    out.close();

    // 2. Generate C Header
    std::ofstream outH(outHPath.string());
    outH <<
      "// Generated by Project Gemini\n#pragma once\n#include \"quickjs.h\"\n\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n";
    outH << "JSModuleDef* js_init_module_" << moduleName << "(JSContext* ctx, const char* module_name);\n\n";
    outH << "#ifdef __cplusplus\n}\n#endif\n";
    outH.close();

    // 3. Generate TypeScript Definitions (.d.ts) [New Section]
    std::ofstream outTS(outTSPath.string());
    outTS << "// Type definitions for " << moduleName << "\n";
    outTS << "// Generated by Project Gemini\n\n";

    // Macros
    if (!macros.empty())
    {
      outTS << "// Constants\n";
      for (const auto& m : macros)
      {
        std::string tsType = (m.value.find('"') != std::string::npos) ? "string" : "number";
        outTS << "export const " << m.name << ": " << tsType << ";\n";
      }
      outTS << "\n";
    }

    // Enums
    if (!enums.empty())
    {
      outTS << "// Enums\n";
      for (const auto& e : enums)
      {
        // Ignore C++ guards in d.ts to provide full API surface
        outTS << "export enum " << e.name << " {\n";
        for (size_t i = 0; i < e.members.size(); ++i)
        {
          outTS << "  " << e.members[i].first << " = " << e.members[i].second;
          if (i < e.members.size() - 1) outTS << ",";
          outTS << "\n";
        }
        outTS << "}\n\n";
      }
    }

    // Functions
    if (!functions.empty())
    {
      outTS << "// Functions\n";
      for (const auto& f : functions)
      {
        // Ignore C++ guards in d.ts
        std::string tsRet = cpp_to_ts_type(f.retType);
        std::string tsArgs = format_ts_args(f.args);
        outTS << "export function " << f.name << "(" << tsArgs << "): " << tsRet << ";\n";
      }
    }
    outTS.close();
  }
};

int main(int argc, char** argv)
{
  if (argc < 4) return 1;
  std::vector<std::string> includes;
  for (int i = 4; i < argc; ++i) includes.emplace_back(argv[i]);

  try
  {
    BindingGenerator gen(argv[1], argv[2], argv[3], includes);
    gen.parse();
    gen.generate();
  }
  catch (const std::exception& e)
  {
    std::cerr << "Generator Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
