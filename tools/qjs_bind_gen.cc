#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <map>
#include <algorithm>
#include <boost/filesystem.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>

namespace fs = boost::filesystem;

struct GuardState
{
  std::string line;
  std::string symbol;
  bool isHeaderGuard;
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
  std::vector<std::string> guards;
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

  int evaluate_expression(std::string expr, const std::map<std::string, int>& symbolTable)
  {
    expr.erase(std::remove(expr.begin(), expr.end(), ' '), expr.end());

    if (expr.find('|') != std::string::npos)
    {
      std::vector<std::string> parts;
      boost::split(parts, expr, boost::is_any_of("|"));
      int result = 0;
      for (const auto& part : parts)
      {
        result |= evaluate_expression(part, symbolTable);
      }
      return result;
    }

    if (expr.length() > 2 && expr.front() == '(' && expr.back() == ')')
    {
      return evaluate_expression(expr.substr(1, expr.length() - 2), symbolTable);
    }

    size_t shiftPos = expr.find("<<");
    if (shiftPos != std::string::npos)
    {
      std::string left = expr.substr(0, shiftPos);
      std::string right = expr.substr(shiftPos + 2);
      return evaluate_expression(left, symbolTable) << evaluate_expression(right, symbolTable);
    }

    size_t plusPos = expr.find("+");
    if (plusPos != std::string::npos)
    {
      std::string left = expr.substr(0, plusPos);
      std::string right = expr.substr(plusPos + 1);
      return evaluate_expression(left, symbolTable) + evaluate_expression(right, symbolTable);
    }

    if (expr.find("0x") == 0 || expr.find("0X") == 0)
    {
      try { return std::stoul(expr, nullptr, 16); }
      catch (...) { return 0; }
    }
    if (isdigit(expr[0]) || (expr.length() > 1 && expr[0] == '-'))
    {
      try { return std::stoi(expr); }
      catch (...) { return 0; }
    }

    if (symbolTable.count(expr))
    {
      return symbolTable.at(expr);
    }

    return 0;
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

  std::string cpp_to_ts_type(std::string cppType)
  {
    std::string t = cppType;
    boost::replace_all(t, "const", "");
    boost::replace_all(t, "volatile", "");
    boost::replace_all(t, "&", "");
    boost::trim(t);

    if (t == "void") return "void";
    if (t == "bool") return "boolean";

    if (t.find("char*") != std::string::npos || t.find("char *") != std::string::npos ||
      t.find("std::string") != std::string::npos)
    {
      return "string";
    }

    static const std::set<std::string> numTypes = {
      "int", "short", "long", "float", "double", "size_t",
      "uint8_t", "int8_t", "uint16_t", "int16_t",
      "uint32_t", "int32_t", "uint64_t", "int64_t", "unsigned int"
    };

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

    for (const auto& e : enums)
    {
      if (t.find(e.name) != std::string::npos) return e.name;
    }

    return "any";
  }

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
      bool isArray = false;

      boost::regex re_extract(R"((.*?)(?:\s+|[*&]+)([\w]+)(\[\])?$)");
      boost::smatch m;

      if (boost::regex_match(argStr, m, re_extract))
      {
        type = m[1].str();
        name = m[2].str();
        if (m[3].matched) isArray = true;

        size_t namePos = argStr.rfind(name);
        if (namePos != std::string::npos)
        {
          type = argStr.substr(0, namePos);
        }
      }
      else
      {
        type = argStr;
        name = "arg" + std::to_string(argCount++);
      }

      boost::trim(type);
      boost::trim(name);

      if (name == "const" || name == "unsigned" || name == "struct" || name == "enum")
      {
        type = argStr;
        name = "arg" + std::to_string(argCount++);
      }

      boost::replace_all(name, "const", "");
      boost::trim(name);

      std::string tsType = cpp_to_ts_type(type);
      if (isArray) tsType += "[]";

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
    if (boost::regex_match(line, m, re_ifdef)) return "#ifndef " + m[1].str();

    boost::regex re_ifndef(R"(^\s*#ifndef\s+(.*))");
    if (boost::regex_match(line, m, re_ifndef)) return "#ifdef " + m[1].str();

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
    boost::regex re_enum_member(R"((\w+)(\s*=\s*([a-zA-Z0-9_]+|-?\d+|0x[0-9a-fA-F]+))?)");

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
            macros.push_back({m[1], m[2], get_active_guards(guardStack)});
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

          // 过滤掉包含 = / new / return 的语句，防止把函数体内的语句当成函数声明
          if (rawRet.find('=') != std::string::npos ||
            rawRet.find("new") != std::string::npos ||
            rawRet.find("return") != std::string::npos ||
            rawRet.find("delete") != std::string::npos)
          {
            buffer.clear();
            continue;
          }

          if (rawArgs.find("...") != std::string::npos)
          {
            buffer.clear();
            continue;
          }
          if (rawRet.find("d2_") != std::string::npos || rawArgs.find("d2_") != std::string::npos)
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
    boost::regex re_complex_macro(R"(\b[A-Z_][A-Z0-9_]*\s*\()");
    if (boost::regex_search(body, re_complex_macro)) return;

    EnumDef edef;
    edef.name = name;
    edef.guards = guards;

    std::map<std::string, int> symbol_table;

    boost::regex re_member(R"(([a-zA-Z0-9_]+)\s*(?:=\s*([^,]+))?)");

    boost::smatch m;
    int currentVal = 0;

    auto start = body.cbegin();
    while (boost::regex_search(start, body.cend(), m, re_member))
    {
      std::string key = m[1];
      std::string val_str = "";

      if (m.size() > 2 && m[2].matched)
      {
        val_str = m[2];
        size_t cpos = val_str.find("//");
        if (cpos != std::string::npos) val_str = val_str.substr(0, cpos);
        cpos = val_str.find("/*");
        if (cpos != std::string::npos) val_str = val_str.substr(0, cpos);
        boost::trim(val_str);
      }

      if (isdigit(key[0]))
      {
        start = m.suffix().first;
        continue;
      }
      if (key == "public" || key == "private" || key == "protected")
      {
        start = m.suffix().first;
        continue;
      }

      int val = currentVal;

      if (!val_str.empty())
      {
        val = evaluate_expression(val_str, symbol_table);
        currentVal = val;
      }

      symbol_table[key] = val;
      edef.members.push_back({key, val});

      currentVal++;
      start = m.suffix().first;
    }

    if (!edef.members.empty())
    {
      enums.push_back(edef);
    }
  }

  void generate()
  {
    fs::path outCppPath = fs::path(outputDir) / (moduleName + "_bind.cpp");
    fs::path outHPath = fs::path(outputDir) / (moduleName + "_bind.h");
    fs::path outTSPath = fs::path(outputDir) / (moduleName + ".d.ts");

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
      for (const auto& g : m.guards) out << g << "\n";
      if (m.value.find('"') != std::string::npos)
      {
        out << "    JS_PROP_STRING_DEF(\"" << m.name << "\", " << m.value << ", JS_PROP_CONFIGURABLE),\n";
      }
      else
      {
        out << "    JS_PROP_DOUBLE_DEF(\"" << m.name << "\", " << m.value << ", JS_PROP_CONFIGURABLE),\n";
      }
      for (size_t i = 0; i < m.guards.size(); ++i) out << "#endif\n";
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

    std::ofstream outH(outHPath.string());
    outH << "// Generated\n#pragma once\n#include \"quickjs.h\"\n\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n";
    outH << "JSModuleDef* js_init_module_" << moduleName << "(JSContext* ctx, const char* module_name);\n\n";
    outH << "#ifdef __cplusplus\n}\n#endif\n";
    outH.close();

    std::ofstream outTS(outTSPath.string());
    outTS << "// Type definitions for " << moduleName << "\n\n";

    if (!macros.empty())
    {
      outTS << "// Constants\n";
      std::set<std::string> exported_macros;
      for (const auto& m : macros)
      {
        if (exported_macros.count(m.name)) continue;
        std::string tsType = (m.value.find('"') != std::string::npos) ? "string" : "number";
        outTS << "export const " << m.name << ": " << tsType << ";\n";
        exported_macros.insert(m.name);
      }
      outTS << "\n";
    }

    if (!enums.empty())
    {
      outTS << "// Enums\n";
      for (const auto& e : enums)
      {
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

    if (!functions.empty())
    {
      outTS << "// Functions\n";
      for (const auto& f : functions)
      {
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
