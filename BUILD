package(default_visibility = ["//visibility:public"])

genrule(
    name = "lager-config",
    srcs = ["lager/config.hpp.in"],
    outs = ["lager/config.hpp"],
    cmd = "cp $< $@",
)

cc_library(
    name = "lager",
    hdrs = glob([
        "lager/**/*.hpp",
    ]) + [":lager-config"],
    deps = ["@boost//:hana"],
    includes = [".", "lager/"],

    visibility = ["//visibility:public"],
    copts = select({
        "@bazel_tools//src/conditions:windows": ["/std:c++17"],
        "//conditions:default": ["-std=c++17"],
    }),
)
