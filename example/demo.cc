#include "quickjs.h"
#include "quickjs-libc.h"
#include <iostream>
#include "my_api_bind.h"

int main(int argc, const char* argv[])
{
  JSRuntime* rt = JS_NewRuntime();
  JSContext* ctx = JS_NewContext(rt);

  JS_SetModuleLoaderFunc(rt, nullptr, js_module_loader, nullptr);
  js_std_add_helpers(ctx, argc, const_cast<char**>(argv));
  js_std_init_handlers(rt);

  js_init_module_my_api(ctx, "my_api");

  const char* js_code = R"(
        import * as api from 'my_api';

        console.log("\x1b[32m--- JS Executing ---\x1b[0m");

        try {
            // --- 1. 基础函数 ---
            console.log("1. Add(10, 20) =", api.add(10, 20));
            api.log_message("Hello from JS Log");

            // --- 2. 结构体传值测试 ---
            console.log("\n\x1b[33m--- Struct Pass-by-Value Test ---\x1b[0m");
            let myCfg = new api.Config();
            myCfg.host = "google.com";
            myCfg.port = 443;
            myCfg.debug_mode = true;

            // JS -> C++ (C++ 打印接收到的值)
            api.print_config(myCfg);

            // --- 3. 结构体返回值测试 ---
            console.log("\n\x1b[33m--- Struct Return-by-Value Test ---\x1b[0m");
            // C++ -> JS
            let defCfg = api.create_default_config();
            console.log("JS Received Default Config:", JSON.parse(defCfg.toJson()));

            // --- 4. 结构体指针修改测试 ---
            console.log("\n\x1b[33m--- Struct Pass-by-Pointer (Modification) Test ---\x1b[0m");
            // 通过工厂创建对象 (返回指针)
            let user = api.create_user("Alice", 1001);
            user.score = 50;
            console.log(`User [${user.name}] Initial Score: ${user.score}`);

            // 传给 C++ 修改
            api.update_user_score(user, 999);

            // 验证 JS 端是否看到修改
            console.log(`User [${user.name}] Updated Score: ${user.score}`);
            if (user.score === 999) {
                console.log("\x1b[32m[SUCCESS] Pointer modification reflected in JS!\x1b[0m");
            } else {
                console.log("\x1b[31m[FAIL] Pointer modification NOT reflected!\x1b[0m");
            }

            // --- 5. Boost.JSON 序列化 ---
            console.log("\n\x1b[33m--- Boost.JSON Serialization ---\x1b[0m");
            console.log("User JSON:", user.toJson());

        } catch(e) {
            console.log("\x1b[31mJS Error Caught:\x1b[0m", e);
            if (e.stack) console.log(e.stack);
        }
        console.log("\x1b[32m--- JS Done ---\x1b[0m");
    )";

  JSValue val = JS_Eval(ctx, js_code, strlen(js_code), "<input>", JS_EVAL_TYPE_MODULE);

  if (JS_IsException(val)) {
    js_std_dump_error(ctx);
  } else if (JS_VALUE_GET_TAG(val) == JS_TAG_MODULE) {
    if (JS_ResolveModule(ctx, val) < 0) {
      js_std_dump_error(ctx);
    } else {
      JSValue ret = JS_EvalFunction(ctx, val);
      if (JS_IsException(ret)) js_std_dump_error(ctx);
      JS_FreeValue(ctx, ret);
    }
  }

  JS_FreeValue(ctx, val);
  std::cout << std::flush;
  std::cerr << std::flush;
  js_std_free_handlers(rt);
  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);
  return 0;
}