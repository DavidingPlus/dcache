add_requires("protobuf-cpp")


target("ProtobufTest")
    set_kind("binary")
    set_languages("c++17")
    add_packages("protobuf-cpp", {public = true})
    add_rules("protobuf.cpp")
    add_files("**.proto", {proto_public = true})
    add_files("*.cpp")
