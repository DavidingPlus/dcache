add_requires("protobuf-cpp")

target("KCacheProtoTest")
    set_kind("binary")
    set_languages("c++17")
    add_packages("protobuf-cpp")

    add_rules("protobuf.cpp")
    add_files("../../src/proto/kcache.proto", {proto_public = true})
    add_files("main.cpp")
target_end()
