# Fuzzing TensorFlow ops with GFT
# Most ops have a similar set of dependencies and a similar fuzzing
# infrastructure. Hence, we gather everything in one single place.
# Note that these fuzzers cover a large part of TF, they are not granular.

load(
    "//tensorflow:tensorflow.bzl",
    "tf_cc_shared_test",
    "tf_cc_test",
)

# copybara:uncomment package(default_applicable_licenses = ["//tensorflow:license"])

# A trivial fuzzer with no pre-specified corpus.
tf_cc_shared_test(
    name = "identity_fuzz",
    srcs = ["identity_fuzz.cc"],
    tags = ["no_oss"],
    deps = [
        "//tensorflow/cc:cc_ops",
        "//tensorflow/core/framework:types_proto_cc",
        "//tensorflow/core/kernels:array",
        "//tensorflow/security/fuzzing/cc:fuzz_session",
        "@com_google_fuzztest//fuzztest",
        "@com_google_fuzztest//fuzztest:fuzztest_gtest_main",
    ],
)

tf_cc_test(
    name = "concat_fuzz",
    srcs = ["concat_fuzz.cc"],
    tags = ["no_oss"],
    deps = [
        "//tensorflow/cc:cc_ops",
        "//tensorflow/core/framework:types_proto_cc",
        "//tensorflow/core/kernels:array",
        "//tensorflow/security/fuzzing/cc:fuzz_session",
        "@com_google_fuzztest//fuzztest",
        "@com_google_fuzztest//fuzztest:fuzztest_gtest_main",
    ],
)

tf_cc_test(
    name = "add_fuzz",
    srcs = ["add_fuzz.cc"],
    tags = ["no_oss"],
    deps = [
        "//tensorflow/cc:cc_ops",
        "//tensorflow/core/framework:types_proto_cc",
        "//tensorflow/core/kernels:array",
        "//tensorflow/security/fuzzing/cc:fuzz_session",
        "@com_google_fuzztest//fuzztest",
        "@com_google_fuzztest//fuzztest:fuzztest_gtest_main",
    ],
)

tf_cc_test(
    name = "matmul_fuzz",
    srcs = ["matmul_fuzz.cc"],
    tags = ["no_oss"],
    deps = [
        "//tensorflow/cc:cc_ops",
        "//tensorflow/core/framework:types_proto_cc",
        "//tensorflow/core/kernels:array",
        "//tensorflow/core/kernels:matmul_op",
        "//tensorflow/security/fuzzing/cc:fuzz_session",
        "@com_google_fuzztest//fuzztest",
        "@com_google_fuzztest//fuzztest:fuzztest_gtest_main",
    ],
)

tf_cc_test(
    name = "bincount_fuzz",
    srcs = ["bincount_fuzz.cc"],
    tags = ["no_oss"],
    deps = [
        "//tensorflow/cc:cc_ops",
        "//tensorflow/core/kernels:array",
        "//tensorflow/core/kernels:bincount_op",
        "//tensorflow/security/fuzzing/cc:fuzz_session",
        "@com_google_fuzztest//fuzztest",
        "@com_google_fuzztest//fuzztest:fuzztest_gtest_main",
    ],
)
