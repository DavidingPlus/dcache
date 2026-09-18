add_requires("conan::etcd-cpp-apiv3/0.15.4", {
    alias = "etcd",
    configs = {
        options = "grpc/*:with_libsystemd=False"
    }
})

target("EtcdClientTest")
    set_kind("binary")
    set_languages("c++17")
    add_files("main.cpp")
    add_packages("etcd")
target_end()
