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
  std::vector<std::pair<std::string, std::string>> members;
  std::vector<std::string> guards;
};

struct MacroDef
{
  std::string name, value;
  std::vector<std::string> guards;
};

struct FieldDef
{
  std::string type;
  std::string name;
};

struct StructDef
{
  std::string name;
  std::vector<FieldDef> fields;
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
  std::vector<StructDef> structs;

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
      for (const auto& part : parts) result |= evaluate_expression(part, symbolTable);
      return result;
    }
    if (expr.length() > 2 && expr.front() == '(' && expr.back() == ')')
      return evaluate_expression(
        expr.substr(1, expr.length() - 2), symbolTable);
    size_t shiftPos = expr.find("<<");
    if (shiftPos != std::string::npos)
      return evaluate_expression(expr.substr(0, shiftPos), symbolTable) <<
        evaluate_expression(expr.substr(shiftPos + 2), symbolTable);
    size_t plusPos = expr.find("+");
    if (plusPos != std::string::npos)
      return evaluate_expression(expr.substr(0, plusPos), symbolTable) +
        evaluate_expression(expr.substr(plusPos + 1), symbolTable);
    if (expr.find("0x") == 0 || expr.find("0X") == 0)
      try { return std::stoul(expr, nullptr, 16); }
      catch (...) { return 0; }
    if (isdigit(expr[0]) || (expr.length() > 1 && expr[0] == '-'))
      try { return std::stoi(expr); }
      catch (...) { return 0; }
    if (symbolTable.count(expr)) return symbolTable.at(expr);
    return 0;
  }

  std::string remove_comments(const std::string& source)
  {
    try
    {
      boost::regex re_block(R"(/\*[\s\S]*?\*/)");
      std::string temp = boost::regex_replace(source, re_block, "");
      boost::regex re_line(R"(//[^\r\n]*)");
      return boost::regex_replace(temp, re_line, "");
    }
    catch (...) { return source; }
  }

  std::string clean_type_string(std::string raw)
  {
    boost::regex re_space(R"([\r\n\t]+)");
    std::string s = boost::regex_replace(raw, re_space, " ");
    boost::regex re_keywords(R"(\b(inline|static|constexpr|extern|virtual|explicit)\b)");
    s = boost::regex_replace(s, re_keywords, "");
    boost::regex re_multi_space(R"(\s+)");
    s = boost::regex_replace(s, re_multi_space, " ");
    return boost::regex_replace(s, boost::regex(R"(^\s+|\s+$)"), "");
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
    if (t.find("char*") != std::string::npos || t.find("string") != std::string::npos) return "string";
    static const std::set<std::string> numTypes = {
      "int", "short", "long", "float", "double", "size_t", "uint8_t", "int8_t", "uint16_t", "int16_t", "uint32_t",
      "int32_t", "uint64_t", "int64_t", "unsigned int"
    };
    for (const auto& nt : numTypes)
      if (t.find(nt) != std::string::npos && t.find("*") == std::string::npos)
        return
          "number";
    for (const auto& e : enums) if (t.find(e.name) != std::string::npos) return e.name;
    for (const auto& s : structs) if (t.find(s.name) != std::string::npos) return s.name;
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
      if (argStr.empty() || argStr == "void") continue;
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
        if (namePos != std::string::npos) type = argStr.substr(0, namePos);
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
    for (const auto& g : stack) if (!g.isHeaderGuard) active.push_back(g.line);
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

  // [New] Check if a type is safe to use in Accessor (Get/Set)
  // Allows basic types and known structs/enums.
  bool is_type_safe_for_binding(std::string type)
  {
    // Normalize
    boost::replace_all(type, "const", "");
    boost::replace_all(type, "volatile", "");
    boost::replace_all(type, "&", ""); // Keep *
    boost::trim(type);

    if (type.find("int") != std::string::npos || type.find("bool") != std::string::npos ||
      type.find("float") != std::string::npos || type.find("double") != std::string::npos ||
      type.find("char") != std::string::npos || type.find("string") != std::string::npos)
      return true;

    // Remove * for struct check
    std::string rawType = type;
    boost::replace_all(rawType, "*", "");
    boost::trim(rawType);

    for (const auto& s : structs) if (rawType == s.name) return true;
    for (const auto& e : enums) if (rawType == e.name) return true;

    return false;
  }

  // [New] Strict check for Boost.JSON supported types
  // Boost.JSON supports: bool, int, double, string, nullptr.
  // It does NOT support arbitrary pointers (except char*) or custom structs directly.
  bool is_json_safe(std::string type)
  {
    // Normalize
    boost::replace_all(type, "const", "");
    boost::replace_all(type, "volatile", "");
    boost::replace_all(type, "&", "");
    boost::trim(type);

    // 1. Strings (std::string or char*)
    if (type == "std::string" || type == "string") return true;
    // char* is okay (treated as string), but int* is not.
    if (type.find("*") != std::string::npos)
    {
      if (type.find("char") != std::string::npos) return true;
      return false; // Other pointers are unsafe for JSON
    }

    // 2. Bool
    if (type == "bool") return true;

    // 3. Floating point
    if (type == "float" || type == "double") return true;

    // 4. Exact Integer Types (White-list)
    // Using a set for O(1) lookup
    static const std::set<std::string> safe_int_types = {
      "int", "signed int", "unsigned int",
      "short", "signed short", "unsigned short",
      "long", "signed long", "unsigned long",
      "long long", "signed long long", "unsigned long long",
      "int8_t", "uint8_t",
      "int16_t", "uint16_t",
      "int32_t", "uint32_t",
      "int64_t", "uint64_t",
      "size_t", "char", "unsigned char", "signed char"
    };

    if (safe_int_types.count(type)) return true;

    return false;
  }

  void process_enum(std::string name, std::string body, const std::vector<std::string>& guards)
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
      if (isdigit(key[0]) || key == "public" || key == "private")
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
      edef.members.push_back({key, val_str.empty() ? std::to_string(val) : val_str});
      currentVal++;
      start = m.suffix().first;
    }
    if (!edef.members.empty()) enums.push_back(edef);
  }

  void process_struct(std::string name, std::string body, const std::vector<std::string>& guards)
  {
    StructDef sdef;
    sdef.name = name;
    sdef.guards = guards;
    boost::regex re_field(R"(([a-zA-Z0-9_:<>\*&\s]+?)\s+(\w+)\s*(?::\s*\d+)?\s*;\s*)");
    auto start = body.cbegin();
    boost::smatch m;
    while (boost::regex_search(start, body.cend(), m, re_field))
    {
      std::string type = clean_type_string(m[1]);
      std::string fname = m[2];
      if (isdigit(fname[0]))
      {
        start = m.suffix().first;
        continue;
      }

      bool is_func_ptr = (type.find("(") != std::string::npos);
      bool is_typedef = (type.find("typedef") != std::string::npos);
      bool is_callback = (type.length() >= 5 && type.substr(type.length() - 5) == "_cb_t") || (type.length() >= 7 &&
        type.substr(type.length() - 7) == "_walker");

      if (!is_func_ptr && !is_typedef && !is_callback)
      {
        boost::trim(type);
        sdef.fields.push_back({type, fname});
      }
      start = m.suffix().first;
    }
    structs.push_back(sdef);
  }

  void parse()
  {
    std::ifstream file(inputPath);
    if (!file.is_open())
    {
      std::cerr << "Error: " << inputPath << std::endl;
      exit(1);
    }
    std::string line;
    std::vector<GuardState> guardStack;
    std::string buffer;
    int brace_depth = 0;

    boost::regex re_macro_val(R"(^\s*#define\s+([A-Z0-9_]+)\s+(\".*\"|-?\d+(\.\d+)?))");
    boost::regex re_define_simple(R"(^\s*#define\s+([A-Z0-9_]+))");
    boost::regex re_ifndef(R"(^\s*#ifndef\s+([A-Z0-9_]+))");
    boost::regex re_elif(R"(^\s*#elif\s+(.*))");
    boost::regex re_enum_cpp(R"(enum\s+(class\s+)?(\w+)\s*\{([\s\S]*?)\};)");
    boost::regex re_enum_c(R"(typedef\s+enum\s*\{([\s\S]*?)\}\s*(\w+);)");
    boost::regex re_struct(R"(struct\s+(\w+)\s*\{([\s\S]*?)\};)");
    boost::regex re_func(R"(([a-zA-Z0-9_:<>\*&\s]+?)\s+(\w+)\s*\(([\s\S]*?)\)\s*(?:;|{))");
    std::set<std::string> blacklist = {"if", "while", "for", "switch", "return", "sizeof", "operator", "else"};
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
      if (trimmed.find("//") != std::string::npos) trimmed = trimmed.substr(0, trimmed.find("//"));
      boost::trim(trimmed);
      if (trimmed.empty()) continue;

      if (trimmed[0] == '#')
      {
        boost::smatch m;
        if (boost::starts_with(trimmed, "#define"))
        {
          if (boost::regex_search(trimmed, m, re_define_simple))
          {
            std::string defSym = m[1];
            if (!guardStack.empty() && guardStack.back().symbol == defSym)
            {
              guardStack.back().isHeaderGuard = true;
              continue;
            }
          }
          if (boost::regex_search(trimmed, m, re_macro_val))
            macros.push_back({
              m[1], m[2], get_active_guards(guardStack)
            });
        }
        else if (boost::starts_with(trimmed, "#if"))
        {
          GuardState gs;
          gs.line = trimmed;
          gs.isHeaderGuard = false;
          if (boost::starts_with(trimmed, "#ifndef") && boost::regex_search(trimmed, m, re_ifndef)) gs.symbol = m[1];
          guardStack.push_back(gs);
        }
        else if (boost::starts_with(trimmed, "#endif")) { if (!guardStack.empty()) guardStack.pop_back(); }
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
            if (boost::regex_search(trimmed, m, re_elif)) guardStack.push_back({"#if " + m[1].str(), "", false});
          }
        }
        continue;
      }

      bool is_extern_c = (line.find("extern \"C\"") != std::string::npos);
      for (char c : trimmed)
      {
        if (c == '{') { if (!is_extern_c) brace_depth++; }
        else if (c == '}') { if (brace_depth > 0) brace_depth--; }
      }
      buffer += trimmed + "\n";

      if (brace_depth == 0 && (trimmed.back() == ';' || trimmed.back() == '}'))
      {
        boost::smatch m;
        if (buffer.find("typedef") != std::string::npos && buffer.find("enum") == std::string::npos)
        {
          buffer.clear();
          continue;
        }
        if (boost::regex_search(buffer, m, re_struct))
        {
          process_struct(m[1], m[2], get_active_guards(guardStack));
          buffer.clear();
          continue;
        }
        if (boost::regex_search(buffer, m, re_enum_cpp))
        {
          process_enum(m[2], m[3], get_active_guards(guardStack));
          buffer.clear();
          continue;
        }
        if (boost::regex_search(buffer, m, re_enum_c))
        {
          process_enum(m[2], m[1], get_active_guards(guardStack));
          buffer.clear();
          continue;
        }
        if (boost::regex_search(buffer, m, re_func))
        {
          std::string rawRet = m[1];
          std::string name = m[2];
          std::string rawArgs = m[3];
          bool skip = false;
          if (rawRet.find('=') != std::string::npos || rawRet.find("new") != std::string::npos ||
            rawRet.find("return") != std::string::npos || rawRet.find("delete") != std::string::npos)
            skip = true;
          if (rawArgs.find("...") != std::string::npos) skip = true;
          if (rawArgs.find("(*") != std::string::npos || rawRet.find("(*") != std::string::npos) skip = true;
          if (rawArgs.find("_cb_t") != std::string::npos || rawRet.find("_cb_t") != std::string::npos) skip = true;
          if (rawArgs.find("_cb") != std::string::npos || rawRet.find("_cb") != std::string::npos) skip = true;
          if (rawArgs.find("_walker") != std::string::npos || rawRet.find("_walker") != std::string::npos) skip = true;
          if (rawArgs.find("_dsc_t") != std::string::npos || rawRet.find("_dsc_t") != std::string::npos) skip = true;
          if (rawArgs.find("_rb_compare_t") != std::string::npos || rawRet.find("_rb_compare_t") != std::string::npos)
            skip = true;
          if (rawArgs.find("_f_t") != std::string::npos || rawRet.find("_f_t") != std::string::npos) skip = true;
          if (rawArgs.find("_handler_t") != std::string::npos || rawRet.find("_handler_t") != std::string::npos) skip =
            true;
          if (rawRet.find("d2_") != std::string::npos || rawArgs.find("d2_") != std::string::npos) skip = true;
          if (blacklist.count(name)) skip = true;
          if (!skip)
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
        buffer.clear();
      }
    }
  }

  void generate()
  {
    fs::path outCppPath = fs::path(outputDir) / (moduleName + "_bind.cpp");
    fs::path outHPath = fs::path(outputDir) / (moduleName + "_bind.h");
    fs::path outTSPath = fs::path(outputDir) / (moduleName + ".d.ts");

    std::ofstream out(outCppPath.string());
    out << "// Generated by Project Gemini\n";
    out << "#include \"quickjs.h\"\n#include \"qjs_utils.hpp\"\n#include <boost/json.hpp>\n";
    for (const auto& inc : extraIncludes)
    {
      if (inc.empty()) continue;
      if (inc.find('<') == std::string::npos && inc.find('"') == std::string::npos)
        out << "#include \"" << inc <<
          "\"\n";
      else out << "#include " << inc << "\n";
    }

    // 1. Structs
    for (const auto& s : structs)
    {
      for (const auto& g : s.guards) out << g << "\n";
      std::string classId = "js_" + s.name + "_class_id";
      out << "static JSClassID " << classId << ";\n";
      out << "template<> JSClassID JSClassIdTraits<" << s.name << ">::id = 0;\n";
      out << "static void js_" << s.name << "_finalizer(JSRuntime *rt, JSValue val) {\n";
      out << "    " << s.name << "* ptr = (" << s.name << "*)JS_GetOpaque(val, " << classId << ");\n";
      out << "    if (ptr) delete ptr;\n";
      out << "}\n";
      out << "static JSValue js_" << s.name <<
        "_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {\n";
      out << "    " << s.name << "* obj = new " << s.name << "();\n";
      out << "    JSValue val = JS_NewObjectClass(ctx, " << classId << ");\n";
      out << "    JS_SetOpaque(val, obj);\n";
      out << "    return val;\n";
      out << "}\n";

      std::vector<std::string> valid_fields;
      for (const auto& f : s.fields)
      {
        // [FIX] Ensure safe for accessors (known struct or basic)
        if (!is_type_safe_for_binding(f.type)) continue;

        valid_fields.push_back(f.name);
        out << "static JSValue js_" << s.name << "_get_" << f.name << "(JSContext *ctx, JSValueConst this_val) {\n";
        out << "    " << s.name << "* obj = (" << s.name << "*)JS_GetOpaque(this_val, " << classId << ");\n";
        out << "    if (!obj) return JS_EXCEPTION;\n";
        out << "    return cpp_to_js(ctx, obj->" << f.name << ");\n";
        out << "}\n";
        out << "static JSValue js_" << s.name << "_set_" << f.name <<
          "(JSContext *ctx, JSValueConst this_val, JSValueConst val) {\n";
        out << "    " << s.name << "* obj = (" << s.name << "*)JS_GetOpaque(this_val, " << classId << ");\n";
        out << "    if (!obj) return JS_EXCEPTION;\n";
        out << "    obj->" << f.name << " = js_to_cpp<" << f.type << ">(ctx, val);\n";
        out << "    return JS_UNDEFINED;\n";
        out << "}\n";
      }

      // [FIX] Strict White-list for JSON serialization
      out << "static JSValue js_" << s.name <<
        "_toJson(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {\n";
      out << "    " << s.name << "* obj = (" << s.name << "*)JS_GetOpaque(this_val, " << classId << ");\n";
      out << "    if (!obj) return JS_EXCEPTION;\n";
      out << "    boost::json::object j;\n";
      for (const auto& f : s.fields)
      {
        if (!is_json_safe(f.type)) continue;
        out << "    j[\"" << f.name << "\"] = obj->" << f.name << ";\n";
      }
      out << "    std::string s = boost::json::serialize(j);\n";
      out << "    return JS_NewString(ctx, s.c_str());\n";
      out << "}\n";

      out << "static const JSCFunctionListEntry js_" << s.name << "_proto_funcs[] = {\n";
      for (const auto& name : valid_fields)
      {
        out << "    JS_CGETSET_DEF(\"" << name << "\", js_" << s.name << "_get_" << name << ", js_" << s.name << "_set_"
          << name << "),\n";
      }
      out << "    JS_CFUNC_DEF(\"toJson\", 0, js_" << s.name << "_toJson),\n";
      out << "};\n";
      for (size_t i = 0; i < s.guards.size(); ++i) out << "#endif\n";
      out << "\n";
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
        out << "    JS_PROP_STRING_DEF(\"" << m.name << "\", " << m.value <<
          ", JS_PROP_CONFIGURABLE),\n";
      else out << "    JS_PROP_DOUBLE_DEF(\"" << m.name << "\", " << m.value << ", JS_PROP_CONFIGURABLE),\n";
      for (size_t i = 0; i < m.guards.size(); ++i) out << "#endif\n";
    }
    out << "};\n\n";

    out << "extern \"C\" JSModuleDef* js_init_module_" << moduleName << "(JSContext* ctx, const char* module_name) {\n";
    out << "    JSModuleDef* m = JS_NewCModule(ctx, module_name, [](JSContext* ctx, JSModuleDef* m) {\n";
    for (const auto& s : structs)
    {
      {
        for (const auto& g : s.guards) out << g << "\n";
        std::string classId = "js_" + s.name + "_class_id";
        out << "        {\n";
        out << "        JS_NewClassID(JS_GetRuntime(ctx),&" << classId << ");\n";
        out << "        JSClassIdTraits<" << s.name << ">::id = " << classId << ";\n";
        out << "        JSClassDef def = { \"" << s.name << "\", .finalizer = js_" << s.name << "_finalizer };\n";
        out << "        JS_NewClass(JS_GetRuntime(ctx), " << classId << ", &def);\n";
        out << "        JSValue proto = JS_NewObject(ctx);\n";
        out << "        JS_SetPropertyFunctionList(ctx, proto, js_" << s.name << "_proto_funcs, sizeof(js_" << s.name <<
          "_proto_funcs)/sizeof(JSCFunctionListEntry));\n";
        out << "        JS_SetClassProto(ctx, " << classId << ", proto);\n";
        out << "        JSValue ctor = JS_NewCFunction2(ctx, js_" << s.name << "_ctor, \"" << s.name <<
          "\", 0, JS_CFUNC_constructor, 0);\n";
        out << "        JS_SetConstructor(ctx, ctor, proto);\n";
        out << "        JS_SetModuleExport(ctx, m, \"" << s.name << "\", ctor);\n";
        out << "        }\n";
        for (size_t i = 0; i < s.guards.size(); ++i) out << "    #endif\n";
      }
    }
    out << "        if (JS_SetModuleExportList(ctx, m, js_" << moduleName << "_funcs, sizeof(js_" << moduleName <<
      "_funcs)/sizeof(JSCFunctionListEntry)) != 0) return -1;\n";
    for (const auto& e : enums)
    {
      for (const auto& g : e.guards) out << "    " << g << "\n";
      out << "        {\n            JSValue enum_obj = JS_NewObject(ctx);\n";
      for (const auto& mem : e.members)
      {
        if (mem.second.empty())
          out << "            JS_SetPropertyStr(ctx, enum_obj, \"" << mem.first <<
            "\", JS_NewInt32(ctx, 0));\n";
        else
          out << "            JS_SetPropertyStr(ctx, enum_obj, \"" << mem.first << "\", JS_NewInt32(ctx, (int32_t)("
            << mem.second << ")));\n";
      }
      out << "            JS_SetModuleExport(ctx, m, \"" << e.name << "\", enum_obj);\n        }\n";
      for (size_t i = 0; i < e.guards.size(); ++i) out << "    #endif\n";
    }
    out << "        return 0;\n    });\n";
    out << "    if (!m) return nullptr;\n";
    out << "    JS_AddModuleExportList(ctx, m, js_" << moduleName << "_funcs, sizeof(js_" << moduleName <<
      "_funcs)/sizeof(JSCFunctionListEntry));\n";
    for (const auto& e : enums)
    {
      for (const auto& g : e.guards) out << "    " << g << "\n";
      out << "    JS_AddModuleExport(ctx, m, \"" << e.name << "\");\n";
      for (size_t i = 0; i < e.guards.size(); ++i) out << "    #endif\n";
    }
    for (const auto& s : structs)
    {
      for (const auto& g : s.guards) out << "    " << g << "\n";
      out << "    JS_AddModuleExport(ctx, m, \"" << s.name << "\");\n";
      for (size_t i = 0; i < s.guards.size(); ++i) out << "    #endif\n";
    }
    out << "    return m;\n}\n";
    out.close();

    std::ofstream outH(outHPath.string());
    outH << "#pragma once\n#include \"quickjs.h\"\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n";
    outH << "JSModuleDef* js_init_module_" << moduleName << "(JSContext* ctx, const char* module_name);\n";
    outH << "#ifdef __cplusplus\n}\n#endif\n";
    outH.close();

    std::ofstream outTS(outTSPath.string());
    outTS << "// Type definitions\n\n";
    std::set<std::string> exported_macros;
    for (const auto& m : macros)
    {
      if (exported_macros.count(m.name)) continue;
      std::string tsType = (m.value.find('"') != std::string::npos) ? "string" : "number";
      outTS << "export const " << m.name << ": " << tsType << ";\n";
      exported_macros.insert(m.name);
    }
    if (!enums.empty())
    {
      outTS << "// Enums\n";
      for (const auto& e : enums)
      {
        outTS << "export enum " << e.name << " {\n";
        for (size_t i = 0; i < e.members.size(); ++i)
        {
          std::string key = e.members[i].first;
          std::string val = e.members[i].second;

          // [FIX] TS Enum Cleanups

          // 1. Remove C++ suffixes (L, U, UL...) from numbers
          static const boost::regex re_dec_suffix(R"(\b(\d+)([UuLl]+)\b)");
          val = boost::regex_replace(val, re_dec_suffix, "$1");
          static const boost::regex re_hex_suffix(R"(\b(0x[0-9a-fA-F]+)([UuLl]+)\b)");
          val = boost::regex_replace(val, re_hex_suffix, "$1");

          // 2. Check if the value is a valid TS number literal (Hex or Decimal)
          // If it contains identifiers (e.g. LV_BAR_...), TS cannot handle it easily without imports.
          // Strategy: If it looks like an external alias, DO NOT emit the value assignment in TS.
          // This keeps the d.ts valid (key exists), even if the value is auto-generated by TS.

          bool is_numeric = true;
          // Allow: digits, x, -, +, (, ), |, <<, >>, space
          // Disallow: alpha characters (except 0x prefix)

          std::string check_str = val;
          boost::to_lower(check_str);

          for (char c : check_str)
          {
            if (isalpha(c) && c != 'x')
            {
              // 'x' for 0x
              is_numeric = false;
              break;
            }
          }

          // Special check for hex prefix integrity (to distinguish 'x' variable from '0x')
          if (val.find("0x") == std::string::npos && val.find("0X") == std::string::npos)
          {
            if (check_str.find('x') != std::string::npos) is_numeric = false;
          }

          if (is_numeric && !val.empty())
          {
            outTS << "  " << key << " = " << val << ",\n";
          }
          else
          {
            // It's an alias or complex expression involving macros.
            // Just emit the key to ensure compilation.
            outTS << "  " << key << ",\n";
          }
        }
        outTS << "}\n\n";
      }
    }
    for (const auto& s : structs)
    {
      outTS << "export class " << s.name << " {\n";
      for (const auto& f : s.fields)
      {
        if (!is_type_safe_for_binding(f.type)) continue;
        outTS << "  " << f.name << ": " << cpp_to_ts_type(f.type) << ";\n";
      }
      outTS << "  toJson(): string;\n}\n\n";
    }
    for (const auto& f : functions)
    {
      outTS << "export function " << f.name << "(" << format_ts_args(f.args) << "): " << cpp_to_ts_type(f.retType) <<
        ";\n";
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
