#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#include <etcd/SyncClient.hpp>


namespace
{

    int report_failure(const char *operation, const etcd::Response &response)
    {
        std::cerr << operation << " failed: error_code=" << response.error_code()
                  << ", message=" << response.error_message() << '\n';
        return EXIT_FAILURE;
    }

} // namespace


int main(int argc, char **argv)
{
    const std::string endpoint = argc > 1 ? argv[1] : "http://127.0.0.1:2379";
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string key = "/kcache/snippet/etcd-client-test/" + std::to_string(timestamp);
    const std::string expected_value = "etcd-cpp-apiv3 works";

    std::cout << "connecting to " << endpoint << '\n';

    etcd::SyncClient client(endpoint);

    const auto set_response = client.set(key, expected_value);
    if (!set_response.is_ok())
    {
        return report_failure("set", set_response);
    }

    const auto get_response = client.get(key);
    if (!get_response.is_ok())
    {
        client.rm(key);
        return report_failure("get", get_response);
    }

    if (get_response.value().as_string() != expected_value)
    {
        client.rm(key);
        std::cerr << "get returned an unexpected value: "
                  << get_response.value().as_string() << '\n';
        return EXIT_FAILURE;
    }

    const auto remove_response = client.rm(key);
    if (!remove_response.is_ok())
    {
        return report_failure("rm", remove_response);
    }

    std::cout << "etcd client set/get/rm test passed\n";
}
