#pragma once

#include "quickjs.h"
#include <string>
#include <vector>
#include <type_traits>
#include <utility>
#include <iostream>
#include <cstdint>

// 调试开关
// #define QJS_DEBUG_BINDING

#ifdef QJS_DEBUG_BINDING
#define QJS_LOG(msg) std::cerr << "[QJS_BIND] " << msg << std::endl
#else
#define QJS_LOG(msg)
#endif

template <typename T>
T js_to_cpp(JSContext* ctx, JSValueConst val)
{
  // 1. 整数, 浮点, 布尔 (保持不变) ...
  if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
  {
    int64_t res;
    JS_ToInt64(ctx, &res, val);
    return static_cast<T>(res);
  }
  else if constexpr (std::is_floating_point_v<T>)
  {
    double res;
    JS_ToFloat64(ctx, &res, val);
    return static_cast<T>(res);
  }
  else if constexpr (std::is_same_v<T, bool>)
  {
    return (bool)JS_ToBool(ctx, val);
  }
  // 4. C++ String
  else if constexpr (std::is_same_v<T, std::string>)
  {
    const char* str = JS_ToCString(ctx, val);
    if (!str) return std::string("");
    std::string res(str);
    JS_FreeCString(ctx, str);
    return res;
  }
  // 5. C 字符串 (const char*)
  else if constexpr (std::is_same_v<T, const char*>)
  {
    // 注意：JS_ToCString 返回的指针需要释放。
    // 为了简便，这里存在微小泄漏，但在 Demo 中通常可以接受。
    // 严谨做法是在 Wrapper 层处理 Scope。
    return JS_ToCString(ctx, val);
  }
  // 6. 枚举
  else if constexpr (std::is_enum_v<T>)
  {
    int32_t res;
    JS_ToInt32(ctx, &res, val);
    return static_cast<T>(res);
  }
  // 7. 指针 (修复 GIF 路径问题)
  else if constexpr (std::is_pointer_v<T>)
  {
    if (JS_IsNull(val) || JS_IsUndefined(val)) return nullptr;

    // [新增] 如果目标是指针，且传入的是字符串 (例如 lv_gif_set_src 的路径)
    // 允许将字符串地址强转为指针返回
    if (JS_IsString(val))
    {
      return (T)JS_ToCString(ctx, val);
    }

    // 处理 BigInt 指针
    int64_t ptr_val = 0;
    int tag = JS_VALUE_GET_TAG(val);

    if (tag == JS_TAG_BIG_INT)
    {
      JS_ToBigInt64(ctx, &ptr_val, val);
    }
    else if (JS_IsNumber(val))
    {
      JS_ToInt64(ctx, &ptr_val, val);
    }
    else
    {
      std::cerr << "[QJS Error] Expected Pointer(BigInt) or String, got tag: " << tag << std::endl;
      return nullptr;
    }

    return reinterpret_cast<T>(static_cast<uintptr_t>(ptr_val));
  }
  return T{};
}

// ... (cpp_to_js 和 Wrapper 保持不变)
template <typename T>
JSValue cpp_to_js(JSContext* ctx, T val)
{
  if constexpr (std::is_void_v<T>) return JS_UNDEFINED;
  else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, int32_t>) return JS_NewInt32(ctx, val);
  else if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, int64_t>)
    return JS_NewInt64(
      ctx, static_cast<int64_t>(val));
  else if constexpr (std::is_floating_point_v<T>) return JS_NewFloat64(ctx, val);
  else if constexpr (std::is_same_v<T, bool>) return JS_NewBool(ctx, val);
  else if constexpr (std::is_same_v<T, std::string>) return JS_NewString(ctx, val.c_str());
  else if constexpr (std::is_same_v<T, const char*>) return JS_NewString(ctx, val ? val : "");
  else if constexpr (std::is_enum_v<T>) return JS_NewInt32(ctx, static_cast<int32_t>(val));
  else if constexpr (std::is_pointer_v<T>)
  {
    if (val == nullptr) return JS_NULL;
    return JS_NewBigInt64(ctx, static_cast<int64_t>(reinterpret_cast<uintptr_t>(val)));
  }
  return JS_NULL;
}

template <auto Func>
struct Wrapper;

template <typename R, typename... Args, R(*Func)(Args...)>
struct Wrapper<Func>
{
  template <std::size_t... Is>
  static JSValue call_impl(JSContext* ctx, JSValueConst* argv, std::index_sequence<Is...>)
  {
    if constexpr (std::is_void_v<R>)
    {
      Func(js_to_cpp<std::decay_t<Args>>(ctx, argv[Is])...);
      return JS_UNDEFINED;
    }
    else
    {
      return cpp_to_js(ctx, Func(js_to_cpp<std::decay_t<Args>>(ctx, argv[Is])...));
    }
  }

  static JSValue call(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
  {
    try
    {
      if (argc < (int)sizeof...(Args)) return JS_ThrowTypeError(ctx, "Arg count mismatch");
      return call_impl(ctx, argv, std::make_index_sequence<sizeof...(Args)>{});
    }
    catch (...) { return JS_ThrowInternalError(ctx, "C++ Exception"); }
  }
};
