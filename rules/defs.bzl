def _qjs_binding_impl(ctx):
    out_cpp = ctx.actions.declare_file(ctx.attr.module_name + "_bind.cpp")
    out_h = ctx.actions.declare_file(ctx.attr.module_name + "_bind.h")

    args = ctx.actions.args()
    args.add(ctx.file.header.path)
    args.add(out_cpp.dirname)
    args.add(ctx.attr.module_name)

    # 遍历列表，添加所有 include 到参数中
    # C++ main 函数会从 argv[4] 开始依次读取它们
    for inc in ctx.attr.include_list:
        args.add(inc)

    ctx.actions.run(
        inputs = [ctx.file.header],
        outputs = [out_cpp, out_h],
        executable = ctx.executable._generator,
        arguments = [args],
        mnemonic = "QJSBindingGen",
        progress_message = "Generating QuickJS bindings for %s" % ctx.attr.module_name,
    )

    return [DefaultInfo(files = depset([out_cpp, out_h]))]

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

    qjs_binding_gen(
        name = gen_name,
        header = header,
        module_name = module_name,
        include_list = include_list,  # 传递列表
    )

    native.cc_library(
        name = name,
        srcs = [":" + gen_name],
        hdrs = [":" + gen_name, header],
        includes = includes,
        deps = deps + [
            "@quickjs-ng",
            "@rules_quickjs_bind_gen//tools:qjs_utils",
        ],
        alwayslink = True,
    )
