#include "quickjs.h"
#include "quickjs-libc.h"
#include <iostream>
#include "my_api_bind.h"


int main(int argc, const char* argv[])
{
  JSRuntime* rt = JS_NewRuntime();
  JSContext* ctx = JS_NewContext(rt);

  // 关键：设置 Loader
  JS_SetModuleLoaderFunc(rt, nullptr, js_module_loader, nullptr);
  js_std_add_helpers(ctx, argc, const_cast<char**>(argv));
  js_std_init_handlers(rt);

  js_init_module_my_api(ctx, "my_api");

  const char* js_code = R"(
        import * as api from 'my_api';

        console.log("--- JS Executing ---");

        try {
            console.log("1. Calling add(10, 20)...");
            let res = api.add(10, 20);
            console.log("   Result:", res);

            console.log("2. Calling multiply(5, 6)...");
            console.log("   Result:", api.multiply(5, 6));
            console.log("   get_server_name:", api.get_server_name());
            api.log_message("   Result log_message");

            console.log("3. Checking Enum...");
            console.log("   State:", api.SystemState.READY);

            console.log("4. Checking Version...");
            console.log("   Version:", api.API_VERSION);
        } catch(e) {
            console.log("JS Error Caught:", e);
        }
        console.log("--- JS Done ---");
    )";

  JSValue val = JS_Eval(ctx, js_code, strlen(js_code), "<input>", JS_EVAL_TYPE_MODULE);

  if (JS_IsException(val))
  {
    js_std_dump_error(ctx);
  }
  else if (JS_VALUE_GET_TAG(val) == JS_TAG_MODULE)
  {
    if (JS_ResolveModule(ctx, val) < 0)
    {
      js_std_dump_error(ctx);
    }
    else
    {
      JSValue ret = JS_EvalFunction(ctx, val);
      if (JS_IsException(ret))
      {
        js_std_dump_error(ctx);
      }
      JS_FreeValue(ctx, ret);
    }
  }

  JS_FreeValue(ctx, val);

  // 强制刷新输出流，防止日志丢失
  std::cout << std::flush;
  std::cerr << std::flush;

  js_std_free_handlers(rt);
  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);
  return 0;
}
