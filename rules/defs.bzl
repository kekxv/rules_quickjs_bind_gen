load("@aspect_rules_js//js:defs.bzl", "js_library")

def _qjs_binding_impl(ctx):
    # 1. 声明输出文件：必须包含 .d.ts
    out_cpp = ctx.actions.declare_file(ctx.attr.module_name + "_bind.cpp")
    out_h = ctx.actions.declare_file(ctx.attr.module_name + "_bind.h")
    out_ts = ctx.actions.declare_file(ctx.attr.module_name + ".d.ts")  # <--- 新增这行

    args = ctx.actions.args()
    args.add(ctx.file.header.path)
    args.add(out_cpp.dirname)
    args.add(ctx.attr.module_name)

    # 遍历列表，添加所有 include 到参数中
    for inc in ctx.attr.include_list:
        args.add(inc)

    ctx.actions.run(
        inputs = [ctx.file.header],
        # 2. 将 .d.ts 添加到 outputs 列表
        outputs = [out_cpp, out_h, out_ts],  # <--- 修改这里
        executable = ctx.executable._generator,
        arguments = [args],
        mnemonic = "QJSBindingGen",
        progress_message = "Generating QuickJS bindings (and types) for %s" % ctx.attr.module_name,
    )

    # 3. 返回 DefaultInfo，这样 bazel build 才会真正生成它
    # 注意：cc_library 通常会忽略 .d.ts 文件，所以放在这里是安全的
    return [DefaultInfo(files = depset([out_cpp, out_h, out_ts]))]

qjs_binding_gen = rule(
    implementation = _qjs_binding_impl,
    attrs = {
        "header": attr.label(allow_single_file = [".h", ".hpp"], mandatory = True),
        "module_name": attr.string(mandatory = True),
        "include_list": attr.string_list(default = []),
        "_generator": attr.label(
            default = Label("@rules_quickjs_bind_gen//tools:qjs_bind_gen"),
            executable = True,
            cfg = "exec",
        ),
    },
)

# 封装宏
def qjs_cc_library(name, header, module_name, includes = [], include_list = [], deps = []):
    gen_name = name + "_gen"
    ts_target_name = name + "_ts"  # 新增一个 target名字

    qjs_binding_gen(
        name = gen_name,
        header = header,
        module_name = module_name,
        include_list = include_list,
    )

    # 用 js_library 包装生成的 .d.ts
    js_library(
        name = ts_target_name,
        # 这里非常关键：我们只需要 .d.ts 文件
        srcs = [":" + gen_name],
        visibility = ["//visibility:public"],
    )

    native.cc_library(
        name = name,
        srcs = [":" + gen_name],
        hdrs = [":" + gen_name, header],
        includes = includes,
        deps = deps + [
            "@quickjs-ng",
            "@boost.json",
            "@rules_quickjs_bind_gen//tools:qjs_utils",
        ],
        alwayslink = True,
    )
