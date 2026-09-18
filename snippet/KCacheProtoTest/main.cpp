#include <iostream>
#include <string>

#include "kcache.pb.h"


int main()
{
    const std::string original_value("value\0with binary data", 22);

    kcache::pb::Request request;
    request.set_group("users");
    request.set_key("user:42");
    request.set_value(original_value);

    std::string serialized;
    if (!request.SerializeToString(&serialized))
    {
        std::cerr << "failed to serialize Request\n";
        return EXIT_FAILURE;
    }

    kcache::pb::Request decoded_request;
    if (!decoded_request.ParseFromString(serialized) ||
        decoded_request.group() != "users" ||
        decoded_request.key() != "user:42" ||
        decoded_request.value() != original_value)
    {
        std::cerr << "failed to deserialize Request\n";
        return EXIT_FAILURE;
    }

    kcache::pb::SetResponse set_response;
    set_response.set_value(true);

    kcache::pb::SetResponse decoded_response;
    if (!decoded_response.ParseFromString(set_response.SerializeAsString()) ||
        !decoded_response.value())
    {
        std::cerr << "failed to deserialize SetResponse\n";
        return EXIT_FAILURE;
    }

    std::cout << "kcache.proto serialization test passed\n"
              << "serialized request size: " << serialized.size() << " bytes\n";
}
