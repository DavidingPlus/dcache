#include <gtest/gtest.h>

#include "dcache.pb.h"

#include <google/protobuf/descriptor.h>

#include <string>


namespace
{

    std::string BinaryValue()
    {
        std::string value = "value";
        value.push_back('\0');
        value += "with";
        value.push_back(static_cast<char>(0xff));
        value += "binary";
        return value;
    }

    void ExpectField(
        const google::protobuf::Descriptor *message,
        const char *name,
        int number,
        google::protobuf::FieldDescriptor::Type type)
    {
        const auto *field = message->FindFieldByName(name);

        ASSERT_NE(nullptr, field);
        EXPECT_EQ(number, field->number());
        EXPECT_EQ(type, field->type());
    }

    template <typename Response>
    void ExpectBooleanResponseRoundTrip()
    {
        Response response;

        EXPECT_FALSE(response.value());
        EXPECT_TRUE(response.SerializeAsString().empty());

        response.set_value(true);
        EXPECT_TRUE(response.value());
        EXPECT_EQ(std::string("\x08\x01", 2), response.SerializeAsString());

        Response decoded;
        ASSERT_TRUE(decoded.ParseFromString(response.SerializeAsString()));
        EXPECT_TRUE(decoded.value());

        decoded.clear_value();
        EXPECT_FALSE(decoded.value());
        EXPECT_TRUE(decoded.SerializeAsString().empty());
    }

} // namespace


TEST(DCacheProtoTests, FileDescriptorDescribesTheExpectedSchema)
{
    const auto *request = dcache::pb::Request::descriptor();

    ASSERT_NE(nullptr, request);
    ASSERT_NE(nullptr, request->file());

    const auto *file = request->file();
    EXPECT_EQ("dcache.proto", file->name());
    EXPECT_EQ("dcache.pb", file->package());
    EXPECT_EQ(5, file->message_type_count());
    EXPECT_EQ(1, file->service_count());

    const auto *request_descriptor = file->FindMessageTypeByName("Request");
    const auto *get_response = file->FindMessageTypeByName("GetResponse");
    const auto *delete_response = file->FindMessageTypeByName("DeleteResponse");
    const auto *set_response = file->FindMessageTypeByName("SetResponse");
    const auto *invalidate_response = file->FindMessageTypeByName("InvalidateResponse");

    ASSERT_NE(nullptr, request_descriptor);
    ASSERT_NE(nullptr, get_response);
    ASSERT_NE(nullptr, delete_response);
    ASSERT_NE(nullptr, set_response);
    ASSERT_NE(nullptr, invalidate_response);

    EXPECT_EQ(request, request_descriptor);
    EXPECT_EQ(3, request_descriptor->field_count());
    EXPECT_EQ(1, get_response->field_count());
    EXPECT_EQ(1, delete_response->field_count());
    EXPECT_EQ(1, set_response->field_count());
    EXPECT_EQ(1, invalidate_response->field_count());

    ExpectField(
        request_descriptor,
        "group",
        1,
        google::protobuf::FieldDescriptor::TYPE_STRING);
    ExpectField(
        request_descriptor,
        "key",
        2,
        google::protobuf::FieldDescriptor::TYPE_STRING);
    ExpectField(
        request_descriptor,
        "value",
        3,
        google::protobuf::FieldDescriptor::TYPE_BYTES);

    ExpectField(
        get_response,
        "value",
        1,
        google::protobuf::FieldDescriptor::TYPE_BYTES);
    ExpectField(
        delete_response,
        "value",
        1,
        google::protobuf::FieldDescriptor::TYPE_BOOL);
    ExpectField(
        set_response,
        "value",
        1,
        google::protobuf::FieldDescriptor::TYPE_BOOL);
    ExpectField(
        invalidate_response,
        "value",
        1,
        google::protobuf::FieldDescriptor::TYPE_BOOL);
}

TEST(DCacheProtoTests, ServiceDescriptorDescribesAllRpcMethods)
{
    const auto *file = dcache::pb::Request::descriptor()->file();
    const auto *service = file->FindServiceByName("DCache");

    ASSERT_NE(nullptr, service);
    EXPECT_EQ("dcache.pb.DCache", service->full_name());
    ASSERT_EQ(4, service->method_count());

    struct ExpectedMethod
    {
        const char *name;
        const char *output_type;
    };

    const ExpectedMethod expected_methods[] = {
        {"Get", "dcache.pb.GetResponse"},
        {"Set", "dcache.pb.SetResponse"},
        {"Delete", "dcache.pb.DeleteResponse"},
        {"Invalidate", "dcache.pb.InvalidateResponse"},
    };

    for (const auto &expected : expected_methods)
    {
        const auto *method = service->FindMethodByName(expected.name);

        ASSERT_NE(nullptr, method);
        EXPECT_EQ("dcache.pb.Request", method->input_type()->full_name());
        EXPECT_EQ(expected.output_type, method->output_type()->full_name());
    }
}

TEST(DCacheProtoTests, RequestHasProto3DefaultValues)
{
    dcache::pb::Request request;

    EXPECT_TRUE(request.IsInitialized());
    EXPECT_TRUE(request.group().empty());
    EXPECT_TRUE(request.key().empty());
    EXPECT_TRUE(request.value().empty());
    EXPECT_EQ(0, request.ByteSizeLong());
    EXPECT_TRUE(request.SerializeAsString().empty());
}

TEST(DCacheProtoTests, RequestRoundTripsTextAndBinaryFields)
{
    dcache::pb::Request request;
    request.set_group("users");
    request.set_key("user:42");
    request.set_value(BinaryValue());

    const auto serialized = request.SerializeAsString();
    ASSERT_FALSE(serialized.empty());

    dcache::pb::Request decoded;
    ASSERT_TRUE(decoded.ParseFromString(serialized));

    EXPECT_EQ("users", decoded.group());
    EXPECT_EQ("user:42", decoded.key());
    EXPECT_EQ(BinaryValue(), decoded.value());
    EXPECT_EQ(serialized, decoded.SerializeAsString());
}

TEST(DCacheProtoTests, RequestUsesTheExpectedWireFieldNumbers)
{
    dcache::pb::Request request;
    request.set_group("users");
    request.set_key("k");
    request.set_value(std::string({'\0', '\x01', static_cast<char>(0xff)}));

    std::string expected;
    expected.push_back(static_cast<char>(0x0a));
    expected.push_back(static_cast<char>(0x05));
    expected += "users";
    expected.push_back(static_cast<char>(0x12));
    expected.push_back(static_cast<char>(0x01));
    expected += "k";
    expected.push_back(static_cast<char>(0x1a));
    expected.push_back(static_cast<char>(0x03));
    expected.append(std::string({'\0', '\x01', static_cast<char>(0xff)}));

    EXPECT_EQ(expected, request.SerializeAsString());
}

TEST(DCacheProtoTests, RequestClearRestoresTheDefaultState)
{
    dcache::pb::Request request;
    request.set_group("users");
    request.set_key("user:42");
    request.set_value(BinaryValue());

    request.Clear();

    EXPECT_TRUE(request.group().empty());
    EXPECT_TRUE(request.key().empty());
    EXPECT_TRUE(request.value().empty());
    EXPECT_EQ(0, request.ByteSizeLong());
    EXPECT_TRUE(request.SerializeAsString().empty());
}

TEST(DCacheProtoTests, RequestCanBeCopiedAndSwapped)
{
    dcache::pb::Request original;
    original.set_group("users");
    original.set_key("user:42");
    original.set_value(BinaryValue());

    dcache::pb::Request copied(original);
    EXPECT_EQ(original.SerializeAsString(), copied.SerializeAsString());

    dcache::pb::Request swapped;
    swapped.set_key("other");
    swapped.Swap(&copied);

    EXPECT_EQ("other", copied.key());
    EXPECT_EQ("user:42", swapped.key());
    EXPECT_EQ(BinaryValue(), swapped.value());
}

TEST(DCacheProtoTests, RequestRejectsMalformedWireData)
{
    dcache::pb::Request request;

    const std::string truncated_length_delimited{
        static_cast<char>(0x0a),
        static_cast<char>(0x05),
        'x'};
    EXPECT_FALSE(request.ParseFromString(truncated_length_delimited));

    const std::string invalid_zero_tag{'\0'};
    EXPECT_FALSE(request.ParseFromString(invalid_zero_tag));
}

TEST(DCacheProtoTests, RequestPreservesUnknownFields)
{
    // Field 4, encoded as a varint with value 42, is not currently defined by
    // dcache.proto. It simulates a field added by a newer protocol version.
    const std::string wire_with_unknown_field{
        static_cast<char>(0x20),
        static_cast<char>(0x2a)};

    dcache::pb::Request request;
    ASSERT_TRUE(request.ParseFromString(wire_with_unknown_field));

    const auto &unknown_fields = request.unknown_fields();
    ASSERT_EQ(1, unknown_fields.field_count());
    EXPECT_EQ(4, unknown_fields.field(0).number());
    EXPECT_EQ(
        google::protobuf::UnknownField::TYPE_VARINT,
        unknown_fields.field(0).type());
    EXPECT_EQ(42u, unknown_fields.field(0).varint());

    dcache::pb::Request round_tripped;
    ASSERT_TRUE(round_tripped.ParseFromString(request.SerializeAsString()));
    ASSERT_EQ(1, round_tripped.unknown_fields().field_count());
    EXPECT_EQ(4, round_tripped.unknown_fields().field(0).number());
    EXPECT_EQ(42u, round_tripped.unknown_fields().field(0).varint());
}

TEST(DCacheProtoTests, GetResponseRoundTripsBinaryPayload)
{
    dcache::pb::GetResponse response;
    response.set_value(BinaryValue());

    dcache::pb::GetResponse decoded;
    ASSERT_TRUE(decoded.ParseFromString(response.SerializeAsString()));

    EXPECT_EQ(BinaryValue(), decoded.value());
    EXPECT_EQ(response.ByteSizeLong(), decoded.ByteSizeLong());
}

TEST(DCacheProtoTests, SetResponseRoundTripsBooleanValue)
{
    ExpectBooleanResponseRoundTrip<dcache::pb::SetResponse>();
}

TEST(DCacheProtoTests, DeleteResponseRoundTripsBooleanValue)
{
    ExpectBooleanResponseRoundTrip<dcache::pb::DeleteResponse>();
}

TEST(DCacheProtoTests, InvalidateResponseRoundTripsBooleanValue)
{
    ExpectBooleanResponseRoundTrip<dcache::pb::InvalidateResponse>();
}
