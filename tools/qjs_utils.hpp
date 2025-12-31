#pragma once

#include "quickjs.h"
#include <string>
#include <vector>
#include <type_traits>
#include <utility>
#include <iostream>
#include <cstdint>

// Debug Macro
// #define QJS_DEBUG_BINDING

#ifdef QJS_DEBUG_BINDING
    #define QJS_LOG(msg) std::cerr << "[QJS_BIND] " << msg << std::endl
#else
    #define QJS_LOG(msg)
#endif

// [New] Forward declaration or definition for QJSCallback if not defined elsewhere
// This ensures it is available for js_to_cpp specialization
#ifndef QJS_CALLBACK_DEFINED
#define QJS_CALLBACK_DEFINED
struct QJSCallback {
    JSContext* ctx;
    JSValueConst value;
};
#endif

// --- 1. Type Traits for Class ID Mapping ---
template<typename T>
struct JSClassIdTraits {
    inline static JSClassID id = 0;
};

// --- 2. Conversion: JS -> C++ ---

template <typename T>
T js_to_cpp(JSContext* ctx, JSValueConst val) {
    using BaseType = std::decay_t<std::remove_pointer_t<T>>;

    // [Special] QJSCallback
    if constexpr (std::is_same_v<T, QJSCallback>) {
        if (!JS_IsFunction(ctx, val)) return {ctx, JS_UNDEFINED};
        return {ctx, val};
    }

    // Struct / Class Object
    if (JSClassIdTraits<BaseType>::id != 0) {
        void* opaque = JS_GetOpaque(val, JSClassIdTraits<BaseType>::id);

        if (!opaque) {
            if (JS_IsNull(val) || JS_IsUndefined(val)) {
                if constexpr (std::is_pointer_v<T>) return nullptr;
                else return BaseType{};
            }
            // std::cerr << "[QJS Error] Invalid object type" << std::endl;
            if constexpr (std::is_pointer_v<T>) return nullptr;
            else return BaseType{};
        }

        if constexpr (std::is_pointer_v<T>) {
            return static_cast<T>(opaque);
        } else {
            return *static_cast<BaseType*>(opaque);
        }
    }

    // Integers
    if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
        int64_t res;
        if (JS_IsBigInt(val)) JS_ToBigInt64(ctx, &res, val);
        else JS_ToInt64(ctx, &res, val);
        return static_cast<T>(res);
    }
    // Floats
    else if constexpr (std::is_floating_point_v<T>) {
        double res; JS_ToFloat64(ctx, &res, val); return static_cast<T>(res);
    }
    // Bool
    else if constexpr (std::is_same_v<T, bool>) {
        return (bool)JS_ToBool(ctx, val);
    }
    // std::string
    else if constexpr (std::is_same_v<T, std::string>) {
        const char* str = JS_ToCString(ctx, val);
        if (!str) return std::string("");
        std::string res(str);
        JS_FreeCString(ctx, str);
        return res;
    }
    // const char*
    else if constexpr (std::is_same_v<T, const char*>) {
        return JS_ToCString(ctx, val);
    }
    // Enums
    else if constexpr (std::is_enum_v<T>) {
        int32_t res; JS_ToInt32(ctx, &res, val); return static_cast<T>(res);
    }
    // Pointers (Generic)
    else if constexpr (std::is_pointer_v<T>) {
        if (JS_IsNull(val) || JS_IsUndefined(val)) return nullptr;

        // [New] Allow string to char* (if T is char* or void*)
        // This is dangerous but useful for APIs like lv_img_set_src taking file paths
        if (JS_IsString(val)) return (T)JS_ToCString(ctx, val);

        int64_t ptr_val = 0;
        if (JS_IsBigInt(val)) JS_ToBigInt64(ctx, &ptr_val, val);
        else JS_ToInt64(ctx, &ptr_val, val);

        return reinterpret_cast<T>(static_cast<uintptr_t>(ptr_val));
    }

    return T{};
}

// --- 3. Conversion: C++ -> JS ---

template <typename T>
JSValue cpp_to_js(JSContext* ctx, T val) {
    using BaseType = std::decay_t<std::remove_pointer_t<T>>;

    // Struct Value (T = Config)
    if constexpr (!std::is_pointer_v<T> && !std::is_void_v<T> && !std::is_integral_v<T> && !std::is_floating_point_v<T> && !std::is_same_v<T, std::string> && !std::is_same_v<T, const char*> && !std::is_enum_v<T>) {
        if (JSClassIdTraits<BaseType>::id != 0) {
            JSValue obj = JS_NewObjectClass(ctx, JSClassIdTraits<BaseType>::id);
            if (JS_IsException(obj)) return obj;
            BaseType* ptr = new BaseType(val);
            JS_SetOpaque(obj, ptr);
            return obj;
        }
    }

    // Pointers (T = User*)
    if constexpr (std::is_pointer_v<T>) {
        if (val == nullptr) return JS_NULL;

        // Struct Pointer
        if (JSClassIdTraits<BaseType>::id != 0) {
            JSValue obj = JS_NewObjectClass(ctx, JSClassIdTraits<BaseType>::id);
            if (JS_IsException(obj)) return obj;
            // [FIX] Cast away const because JS_SetOpaque takes void*
            JS_SetOpaque(obj, const_cast<void*>(static_cast<const void*>(val)));
            return obj;
        }

        // Generic Pointer -> BigInt
        return JS_NewBigInt64(ctx, static_cast<int64_t>(reinterpret_cast<uintptr_t>(val)));
    }

    // Basic Types
    if constexpr (std::is_void_v<T>) return JS_UNDEFINED;
    else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, int32_t>) return JS_NewInt32(ctx, val);
    else if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, int64_t>) return JS_NewInt64(ctx, static_cast<int64_t>(val));
    else if constexpr (std::is_floating_point_v<T>) return JS_NewFloat64(ctx, val);
    else if constexpr (std::is_same_v<T, bool>) return JS_NewBool(ctx, val);
    else if constexpr (std::is_same_v<T, std::string>) return JS_NewString(ctx, val.c_str());
    else if constexpr (std::is_same_v<T, const char*>) return JS_NewString(ctx, val ? val : "");
    else if constexpr (std::is_enum_v<T>) return JS_NewInt32(ctx, static_cast<int32_t>(val));

    return JS_NULL;
}

// --- 4. Wrapper Helper ---

template<auto Func>
struct Wrapper;

template<typename R, typename... Args, R(*Func)(Args...)>
struct Wrapper<Func> {
    template<std::size_t... Is>
    static JSValue call_impl(JSContext* ctx, JSValueConst* argv, std::index_sequence<Is...>) {
        if constexpr (std::is_void_v<R>) {
            Func(js_to_cpp<std::decay_t<Args>>(ctx, argv[Is])...);
            return JS_UNDEFINED;
        } else {
            return cpp_to_js(ctx, Func(js_to_cpp<std::decay_t<Args>>(ctx, argv[Is])...));
        }
    }

    static JSValue call(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        try {
            if (argc < (int)sizeof...(Args)) {
                return JS_ThrowTypeError(ctx, "Arg count mismatch");
            }
            return call_impl(ctx, argv, std::make_index_sequence<sizeof...(Args)>{});
        } catch (...) {
            return JS_ThrowInternalError(ctx, "C++ Exception");
        }
    }
};