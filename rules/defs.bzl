def _qjs_binding_impl(ctx):
    # 声明两个输出文件
    out_cpp = ctx.actions.declare_file(ctx.attr.module_name + "_bind.cpp")
    out_h = ctx.actions.declare_file(ctx.attr.module_name + "_bind.h")

    args = ctx.actions.args()
    args.add(ctx.file.header.path)

    # 生成器接受的是输出目录，我们传 declare_file 的目录
    args.add(out_cpp.dirname)
    args.add(ctx.attr.module_name)

    ctx.actions.run(
        inputs = [ctx.file.header],
        outputs = [out_cpp, out_h],  # 告诉 Bazel 这里会产生两个文件
        executable = ctx.executable._generator,
        arguments = [args],
        mnemonic = "QJSBindingGen",
        progress_message = "Generating QuickJS bindings for %s" % ctx.attr.module_name,
    )

    # 返回所有生成的文件
    return [DefaultInfo(files = depset([out_cpp, out_h]))]

qjs_binding_gen = rule(
    implementation = _qjs_binding_impl,
    attrs = {
        "header": attr.label(allow_single_file = [".h", ".hpp"], mandatory = True),
        "module_name": attr.string(mandatory = True),
        "_generator": attr.label(
            default = Label("//tools:qjs_bind_gen"),
            executable = True,
            cfg = "exec",
        ),
    },
)

def qjs_cc_library(name, header, module_name, deps = []):
    gen_name = name + "_gen"

    qjs_binding_gen(
        name = gen_name,
        header = header,
        module_name = module_name,
    )

    native.cc_library(
        name = name,
        # 将生成的规则同时放入 srcs 和 hdrs
        # Bazel 会自动识别 .cpp 编译，.h 用于头文件包含
        srcs = [":" + gen_name],
        hdrs = [":" + gen_name, header],
        deps = deps + [
            "@quickjs-ng",
            "//tools:qjs_utils",
        ],
        alwayslink = True,
    )
