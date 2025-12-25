#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <boost/filesystem.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>

namespace fs = boost::filesystem;

// 宏保护状态结构
struct GuardState {
    std::string line;       
    std::string symbol;     
    bool isHeaderGuard;     
};

struct FuncDef { 
    std::string retType, name, args; 
    std::vector<std::string> guards; 
};

struct EnumDef { 
    std::string name; 
    std::vector<std::pair<std::string, int>> members; 
    std::vector<std::string> guards;
};

struct MacroDef { std::string name, value; };

class BindingGenerator {
    std::string inputPath;
    std::string outputDir;
    std::string moduleName;
    std::vector<std::string> extraIncludes;

    std::vector<FuncDef> functions;
    std::vector<EnumDef> enums;
    std::vector<MacroDef> macros;

public:
    BindingGenerator(std::string in, std::string out, std::string mod, std::vector<std::string> extras) 
        : inputPath(in), outputDir(out), moduleName(mod), extraIncludes(extras) {}

    std::string remove_comments(const std::string& source) {
        try {
            boost::regex re_block(R"(/\*[\s\S]*?\*/)");
            std::string temp = boost::regex_replace(source, re_block, "");
            boost::regex re_line(R"(//[^\r\n]*)"); 
            std::string clean = boost::regex_replace(temp, re_line, "");
            if (clean.length() < 10 && source.length() > 50) return source;
            return clean;
        } catch (...) {
            return source;
        }
    }

    std::string clean_type_string(std::string raw) {
        boost::regex re_space(R"([\r\n\t]+)");
        std::string s = boost::regex_replace(raw, re_space, " ");
        boost::regex re_keywords(R"(\b(inline|static|constexpr|extern|virtual|explicit)\b)");
        s = boost::regex_replace(s, re_keywords, "");
        boost::regex re_multi_space(R"(\s+)");
        s = boost::regex_replace(s, re_multi_space, " ");
        s = boost::regex_replace(s, boost::regex(R"(^\s+|\s+$)"), "");
        return s;
    }

    std::string clean_args_string(std::string raw) {
        boost::regex re_space(R"([\r\n\t]+)");
        return boost::regex_replace(raw, re_space, " ");
    }

    std::vector<std::string> get_active_guards(const std::vector<GuardState>& stack) {
        std::vector<std::string> active;
        for (const auto& g : stack) {
            if (!g.isHeaderGuard) {
                active.push_back(g.line);
            }
        }
        return active;
    }

    void parse() {
        std::ifstream file(inputPath);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot open file " << inputPath << std::endl;
            exit(1);
        }

        std::string line;
        std::vector<GuardState> guardStack; 
        std::string buffer;

        boost::regex re_macro_val(R"(^\s*#define\s+([A-Z0-9_]+)\s+(\".*\"|-?\d+(\.\d+)?))");
        boost::regex re_ifndef(R"(^\s*#ifndef\s+([A-Z0-9_]+))");
        boost::regex re_define_simple(R"(^\s*#define\s+([A-Z0-9_]+))");

        // Enum C++: enum class Name { ... };
        boost::regex re_enum_cpp(R"(enum\s+(class\s+)?(\w+)\s*\{([^}]*)\};)");
        // Enum C: typedef enum { ... } Name;
        boost::regex re_enum_c(R"(typedef\s+enum\s*\{([^}]*)\}\s*(\w+);)");
        
        boost::regex re_func(R"(([a-zA-Z0-9_:<>\*&\s]+?)\s+(\w+)\s*\(([\s\S]*?)\)\s*(?:;|{))");
        boost::regex re_enum_member(R"((\w+)(\s*=\s*(-?\d+))?)");

        std::set<std::string> blacklist = {
            "if", "while", "for", "switch", "return", "sizeof", "catch", "else", 
            "using", "template", "operator", "void"
        };

        bool in_comment_block = false;

        while (std::getline(file, line)) {
            std::string trimmed = line;
            boost::trim(trimmed);

            // 1. 注释处理
            if (in_comment_block) {
                if (trimmed.find("*/") != std::string::npos) in_comment_block = false;
                continue;
            }
            if (trimmed.find("/*") != std::string::npos) {
                if (trimmed.find("*/") == std::string::npos) in_comment_block = true;
                trimmed = trimmed.substr(0, trimmed.find("/*"));
            }
            if (trimmed.find("//") != std::string::npos) {
                trimmed = trimmed.substr(0, trimmed.find("//"));
                boost::trim(trimmed);
            }
            if (trimmed.empty()) continue;

            // 2. 预处理
            if (trimmed[0] == '#') {
                boost::smatch m;
                if (boost::starts_with(trimmed, "#define")) {
                    if (boost::regex_search(trimmed, m, re_define_simple)) {
                        std::string defSym = m[1];
                        if (!guardStack.empty()) {
                            if (guardStack.back().symbol == defSym) {
                                guardStack.back().isHeaderGuard = true;
                                continue; 
                            }
                        }
                    }
                    if (boost::regex_search(trimmed, m, re_macro_val)) {
                        macros.push_back({m[1], m[2]});
                    }
                }
                else if (boost::starts_with(trimmed, "#if")) {
                    GuardState gs;
                    gs.line = trimmed;
                    gs.isHeaderGuard = false;
                    if (boost::starts_with(trimmed, "#ifndef") && boost::regex_search(trimmed, m, re_ifndef)) {
                        gs.symbol = m[1];
                    }
                    guardStack.push_back(gs);
                }
                else if (boost::starts_with(trimmed, "#endif")) {
                    if (!guardStack.empty()) guardStack.pop_back();
                }
                else if (boost::starts_with(trimmed, "#else") || boost::starts_with(trimmed, "#elif")) {
                    if (!guardStack.empty()) guardStack.pop_back();
                    guardStack.push_back({trimmed, "", false}); 
                }
                continue;
            }

            // 3. 累积代码
            buffer += trimmed + " ";

            // 4. 检查语句结束
            // 注意：我们不仅在 ';' 和 '}' 时检查，也在 '{' 时检查（为了 inline 函数）
            // 但关键是：如果 '{' 检查失败，不能清空 buffer，因为这可能是 enum { 的开始
            bool is_stmt_end = (trimmed.back() == ';' || trimmed.back() == '}');
            bool is_block_start = (trimmed.back() == '{');

            if (is_stmt_end || is_block_start) {
                boost::smatch m;
                
                // 过滤 typedef (非 enum)
                if (buffer.find("typedef") != std::string::npos && buffer.find("enum") == std::string::npos) {
                    if (is_stmt_end) buffer.clear(); 
                    continue; 
                }

                // [FIX] 只有在遇到 ; 或 } 时才尝试匹配 Enum，因为 Enum 必须以 ; 结尾
                if (is_stmt_end) {
                    if (boost::regex_search(buffer, m, re_enum_cpp)) {
                        process_enum(m[2], m[3], get_active_guards(guardStack), re_enum_member);
                        buffer.clear(); continue;
                    }
                    if (boost::regex_search(buffer, m, re_enum_c)) {
                        process_enum(m[2], m[1], get_active_guards(guardStack), re_enum_member);
                        buffer.clear(); continue;
                    }
                }

                // 尝试匹配函数
                // 函数可能以 ; 结尾 (声明) 或 { 结尾 (定义开始)
                if (boost::regex_search(buffer, m, re_func)) {
                    std::string rawRet = m[1];
                    std::string name = m[2];
                    std::string rawArgs = m[3];

                    if (rawArgs.find("...") != std::string::npos) {
                        buffer.clear(); continue;
                    }

                    if (blacklist.count(name) == 0 && rawRet.find('#') == std::string::npos) {
                        std::string cleanRet = clean_type_string(rawRet);
                        if (!cleanRet.empty()) {
                            functions.push_back({cleanRet, name, clean_args_string(rawArgs), get_active_guards(guardStack)});
                            buffer.clear(); continue; // 匹配成功则清除
                        }
                    }
                }

                // [关键修复]
                // 只有明确是语句结束符 (; 或 }) 且没匹配到任何东西时，才认为是垃圾数据并清理。
                // 如果是 { 结尾且没匹配到函数，保留 buffer！因为它可能是 Enum 的开始。
                if (is_stmt_end) {
                    buffer.clear();
                }
            }
        }
    }

    void process_enum(std::string name, std::string body, const std::vector<std::string>& guards, const boost::regex& re_member) {
        EnumDef edef;
        edef.name = name;
        edef.guards = guards;
        boost::smatch m;
        int currentVal = 0;
        auto start = body.cbegin();
        while (boost::regex_search(start, body.cend(), m, re_member)) {
            if(m[1].length() > 0) {
               if (m[3].matched) currentVal = std::stoi(m[3]);
               edef.members.push_back({m[1], currentVal++}); 
            }
            start = m.suffix().first;
        }
        enums.push_back(edef);
    }

    void generate() {
        fs::path outCppPath = fs::path(outputDir) / (moduleName + "_bind.cpp");
        fs::path outHPath = fs::path(outputDir) / (moduleName + "_bind.h");

        std::ofstream out(outCppPath.string());

        out << "// Generated by Project Gemini\n";
        out << "#include \"quickjs.h\"\n";
        out << "#include \"qjs_utils.hpp\"\n";

        for (const auto& inc : extraIncludes) {
            if (inc.empty()) continue;
            if (inc.find('<') == std::string::npos && inc.find('"') == std::string::npos) {
                out << "#include \"" << inc << "\"\n";
            } else {
                out << "#include " << inc << "\n";
            }
        }

        // 不再自动 include inputPath

        out << "\nstatic const JSCFunctionListEntry js_" << moduleName << "_funcs[] = {\n";
        
        for (const auto& f : functions) {
            for(const auto& g : f.guards) out << g << "\n";
            out << "    JS_CFUNC_DEF(\"" << f.name << "\", 0, (Wrapper<" << f.name << ">::call)),\n";
            for(size_t i=0; i<f.guards.size(); ++i) out << "#endif\n";
        }
        
        for (const auto& m : macros) {
            if (m.value.find('"') != std::string::npos) {
                out << "    JS_PROP_STRING_DEF(\"" << m.name << "\", " << m.value << ", JS_PROP_CONFIGURABLE),\n";
            } else {
                out << "    JS_PROP_DOUBLE_DEF(\"" << m.name << "\", " << m.value << ", JS_PROP_CONFIGURABLE),\n";
            }
        }
        out << "};\n\n";

        out << "extern \"C\" JSModuleDef* js_init_module_" << moduleName << "(JSContext* ctx, const char* module_name) {\n";
        out << "    JSModuleDef* m = JS_NewCModule(ctx, module_name, [](JSContext* ctx, JSModuleDef* m) {\n";
        out << "        if (JS_SetModuleExportList(ctx, m, js_" << moduleName << "_funcs, \n";
        out << "                                   sizeof(js_" << moduleName << "_funcs)/sizeof(JSCFunctionListEntry)) != 0) return -1;\n";
        
        for(const auto& e : enums) {
            for(const auto& g : e.guards) out << "    " << g << "\n";
            out << "        {\n";
            out << "            JSValue enum_obj = JS_NewObject(ctx);\n";
            for(const auto& mem : e.members) {
                out << "            JS_SetPropertyStr(ctx, enum_obj, \"" << mem.first << "\", JS_NewInt32(ctx, " << mem.second << "));\n";
            }
            out << "            JS_SetModuleExport(ctx, m, \"" << e.name << "\", enum_obj);\n";
            out << "        }\n";
            for(size_t i=0; i<e.guards.size(); ++i) out << "    #endif\n";
        }
        out << "        return 0;\n";
        out << "    });\n\n";
        out << "    if (!m) return nullptr;\n\n";
        out << "    JS_AddModuleExportList(ctx, m, js_" << moduleName << "_funcs, sizeof(js_" << moduleName << "_funcs)/sizeof(JSCFunctionListEntry));\n";
        
        for(const auto& e : enums) {
            for(const auto& g : e.guards) out << "    " << g << "\n";
            out << "    JS_AddModuleExport(ctx, m, \"" << e.name << "\");\n";
            for(size_t i=0; i<e.guards.size(); ++i) out << "    #endif\n";
        }
        out << "    return m;\n";
        out << "}\n";
        out.close();

        std::ofstream outH(outHPath.string());
        outH << "// Generated by Project Gemini\n#pragma once\n#include \"quickjs.h\"\n\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n";
        outH << "JSModuleDef* js_init_module_" << moduleName << "(JSContext* ctx, const char* module_name);\n\n";
        outH << "#ifdef __cplusplus\n}\n#endif\n";
        outH.close();
    }
};

int main(int argc, char** argv) {
    if (argc < 4) return 1;
    std::vector<std::string> includes;
    for (int i = 4; i < argc; ++i) includes.push_back(argv[i]);

    try {
        BindingGenerator gen(argv[1], argv[2], argv[3], includes);
        gen.parse();
        gen.generate();
    } catch (const std::exception& e) {
        std::cerr << "Generator Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}