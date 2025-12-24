#pragma once

#include "quickjs.h"
#include <string>
#include <vector>
#include <type_traits>
#include <utility>
#include <iostream> // 用于调试输出

// 定义调试宏，如果崩溃依然发生，可以取消下面这行的注释查看详细追踪
// #define QJS_DEBUG_BINDING

#ifdef QJS_DEBUG_BINDING
#define QJS_LOG(msg) std::cerr << "[QJS_BIND] " << msg << std::endl
#else
#define QJS_LOG(msg)
#endif

// --- 类型转换 ---

template <typename T>
T js_to_cpp(JSContext* ctx, JSValueConst val)
{
  if constexpr (std::is_same_v<T, int> || std::is_same_v<T, int32_t>)
  {
    int32_t res;
    if (JS_ToInt32(ctx, &res, val) != 0)
    {
      QJS_LOG("Warning: JS_ToInt32 failed/exception");
    }
    return res;
  }
  else if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>)
  {
    double res;
    JS_ToFloat64(ctx, &res, val);
    return static_cast<T>(res);
  }
  else if constexpr (std::is_same_v<T, bool>)
  {
    return (bool)JS_ToBool(ctx, val);
  }
  else if constexpr (std::is_same_v<T, std::string>)
  {
    const char* str = JS_ToCString(ctx, val);
    if (!str)
    {
      QJS_LOG("Error: JS_ToCString returned NULL");
      return std::string("");
    }
    std::string res(str);
    JS_FreeCString(ctx, str);
    return res;
  }
  else if constexpr (std::is_enum_v<T>)
  {
    int32_t res;
    JS_ToInt32(ctx, &res, val);
    return static_cast<T>(res);
  }
  return T{};
}

template <typename T>
JSValue cpp_to_js(JSContext* ctx, T val)
{
  if constexpr (std::is_void_v<T>)
  {
    return JS_UNDEFINED;
  }
  else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, int32_t>)
  {
    return JS_NewInt32(ctx, val);
  }
  else if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>)
  {
    return JS_NewFloat64(ctx, val);
  }
  else if constexpr (std::is_same_v<T, bool>)
  {
    return JS_NewBool(ctx, val);
  }
  else if constexpr (std::is_same_v<T, std::string>)
  {
    return JS_NewString(ctx, val.c_str());
  }
  else if constexpr (std::is_same_v<T, const char*>)
  {
    return JS_NewString(ctx, val);
  }
  else if constexpr (std::is_enum_v<T>)
  {
    return JS_NewInt32(ctx, static_cast<int32_t>(val));
  }
  return JS_NULL;
}

// --- 包装器 ---

template <auto Func>
struct Wrapper;

// 特化 1: 普通函数
template <typename R, typename... Args, R(*Func)(Args...)>
struct Wrapper<Func>
{
  template <std::size_t... Is>
  static JSValue call_impl(JSContext* ctx, JSValueConst* argv, std::index_sequence<Is...>)
  {
    QJS_LOG("Calling function with " << sizeof...(Args) << " args");
    if constexpr (std::is_void_v<R>)
    {
      Func(js_to_cpp<std::decay_t<Args>>(ctx, argv[Is])...);
      return JS_UNDEFINED;
    }
    else
    {
      // 分开写以确保求值顺序和异常安全
      auto result = Func(js_to_cpp<std::decay_t<Args>>(ctx, argv[Is])...);
      return cpp_to_js(ctx, result);
    }
  }

  static JSValue call(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
  {
    try
    {
      if (argc < (int)sizeof...(Args))
      {
        return JS_ThrowTypeError(ctx, "Arg count mismatch: expected %lu, got %d", sizeof...(Args), argc);
      }
      return call_impl(ctx, argv, std::make_index_sequence<sizeof...(Args)>{});
    }
    catch (const std::exception& e)
    {
      std::cerr << "C++ Exception in Wrapper: " << e.what() << std::endl;
      return JS_ThrowInternalError(ctx, "C++ Exception: %s", e.what());
    }
    catch (...)
    {
      std::cerr << "Unknown C++ Exception in Wrapper" << std::endl;
      return JS_ThrowInternalError(ctx, "Unknown C++ Exception");
    }
  }
};

// 特化 2: noexcept 函数 (C++17 起 noexcept 是类型系统的一部分，必须单独特化)
template <typename R, typename... Args, R(*Func)(Args...) noexcept>
struct Wrapper<Func>
{
  template <std::size_t... Is>
  static JSValue call_impl(JSContext* ctx, JSValueConst* argv, std::index_sequence<Is...>)
  {
    QJS_LOG("Calling noexcept function");
    if constexpr (std::is_void_v<R>)
    {
      Func(js_to_cpp<std::decay_t<Args>>(ctx, argv[Is])...);
      return JS_UNDEFINED;
    }
    else
    {
      auto result = Func(js_to_cpp<std::decay_t<Args>>(ctx, argv[Is])...);
      return cpp_to_js(ctx, result);
    }
  }

  static JSValue call(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
  {
    // noexcept 函数理论上不会抛出 C++ 异常，但为了保险依然包裹
    try
    {
      if (argc < (int)sizeof...(Args))
      {
        return JS_ThrowTypeError(ctx, "Arg count mismatch");
      }
      return call_impl(ctx, argv, std::make_index_sequence<sizeof...(Args)>{});
    }
    catch (...)
    {
      return JS_ThrowInternalError(ctx, "Fatal Error in noexcept wrapper");
    }
  }
};
