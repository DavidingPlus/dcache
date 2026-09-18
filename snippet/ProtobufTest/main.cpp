#include <iostream>
#include <string>

#include "protobuftest.pb.h"


int main()
{
    protobuftest::Request req;
    req.set_query("hello world");

    std::string output;
    if (req.SerializeToString(&output)) std::cout << "Serialized data: " << output << std::endl;
}
