#ifdef NATIVE_TEST

#include <unity.h>
#include <ObjectProperty.h>
#include <VariantProperty.h>
#include <ResourceProperty.h>
#include <MicroList.h>
#include <Reflect.h>
#include <wire/Buffer.h>
#include <wire/TypeCodec.h>
#include <cstring>
#include <vector>

using namespace MicroProto;

void setUp(void) {}
void tearDown(void) {}

// ==== Test Structs for ObjectProperty ====

struct Position {
    int32_t x;
    int32_t y;
};

struct Position3D {
    int32_t x;
    int32_t y;
    int32_t z;
};

struct MixedData {
    uint8_t flag;
    int32_t count;
    float ratio;
};

struct ConfigWithFields {
    Value<uint8_t> brightness{128};
    Value<uint8_t> speed{50};
    Value<bool> enabled{true};
};

// Nested struct for testing
struct Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct NestedStruct {
    Color color;
    int32_t intensity;
};

// Struct with std::array
struct WithArray {
    std::array<uint8_t, 3> rgb;
    int32_t brightness;
};

// Struct with std::vector - NOT wire-safe, cannot be used with ObjectProperty
// struct WithVector {
//     std::vector<uint8_t> pixels;
//     int32_t count;
// };

// Register field names for Position struct
MICROPROTO_FIELD_NAMES(Position, "x", "y");

// Register field names for MixedData struct
MICROPROTO_FIELD_NAMES(MixedData, "flag", "count", "ratio");

// ==== ObjectProperty Tests ====

void test_object_property_basic() {
    ObjectProperty<Position> position("position");

    TEST_ASSERT_EQUAL(TYPE_OBJECT, position.getTypeId());
    TEST_ASSERT_EQUAL(2, position.fieldCount());
    TEST_ASSERT_EQUAL(sizeof(Position), position.getSize());
}

void test_object_property_field_access() {
    ObjectProperty<Position> position("position");

    // Direct struct access via ->
    position->x = 100;
    position->y = 200;

    TEST_ASSERT_EQUAL(100, position->x);
    TEST_ASSERT_EQUAL(200, position->y);
}

void test_object_property_get_field() {
    ObjectProperty<Position3D> position("position");
    position->x = 10;
    position->y = 20;
    position->z = 30;

    // Access by index
    TEST_ASSERT_EQUAL(10, position.getField<0>());
    TEST_ASSERT_EQUAL(20, position.getField<1>());
    TEST_ASSERT_EQUAL(30, position.getField<2>());
}

void test_object_property_initial_value() {
    Position3D initial{100, 200, 300};
    ObjectProperty<Position3D> position("position", initial);

    TEST_ASSERT_EQUAL(100, position->x);
    TEST_ASSERT_EQUAL(200, position->y);
    TEST_ASSERT_EQUAL(300, position->z);
}

void test_object_property_mixed_types() {
    ObjectProperty<MixedData> mixed("mixed");

    mixed->flag = 255;
    mixed->count = -12345;
    mixed->ratio = 3.14f;

    TEST_ASSERT_EQUAL(255, mixed->flag);
    TEST_ASSERT_EQUAL(-12345, mixed->count);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 3.14f, mixed->ratio);
}

void test_object_property_setdata() {
    ObjectProperty<Position> position("position");

    // Set raw data (little endian int32s)
    Position data{100, 200};
    position.setData(&data, sizeof(data));

    TEST_ASSERT_EQUAL(100, position->x);
    TEST_ASSERT_EQUAL(200, position->y);
}

void test_object_property_with_field_wrapper() {
    ObjectProperty<ConfigWithFields> config("config");

    // Access Field-wrapped values
    config->brightness = 200;
    config->speed = 75;
    config->enabled = false;

    TEST_ASSERT_EQUAL(200, config->brightness.value);
    TEST_ASSERT_EQUAL(75, config->speed.value);
    TEST_ASSERT_FALSE(config->enabled.value);

    // Set constraints at runtime
    config->brightness.setRange(10, 200);
    TEST_ASSERT_TRUE(config->brightness.validate(100));
    TEST_ASSERT_FALSE(config->brightness.validate(5));   // Below min
    TEST_ASSERT_FALSE(config->brightness.validate(250)); // Above max (truncated but still fails)
}

void test_object_property_foreach() {
    ObjectProperty<Position3D> position("position");
    position->x = 10;
    position->y = 20;
    position->z = 30;

    int sum = 0;
    position.forEachField([&sum](auto, auto& field) {
        sum += field;
    });

    TEST_ASSERT_EQUAL(60, sum);
}

void test_object_property_nested() {
    ObjectProperty<NestedStruct> nested("nested");

    // Access nested struct
    nested->color.r = 255;
    nested->color.g = 128;
    nested->color.b = 64;
    nested->intensity = 100;

    TEST_ASSERT_EQUAL(255, nested->color.r);
    TEST_ASSERT_EQUAL(128, nested->color.g);
    TEST_ASSERT_EQUAL(64, nested->color.b);
    TEST_ASSERT_EQUAL(100, nested->intensity);

    // Field count should be 2 (color + intensity)
    TEST_ASSERT_EQUAL(2, nested.fieldCount());
}

void test_object_property_with_array() {
    ObjectProperty<WithArray> led("led");

    // Access std::array member
    led->rgb[0] = 255;
    led->rgb[1] = 128;
    led->rgb[2] = 64;
    led->brightness = 50;

    TEST_ASSERT_EQUAL(255, led->rgb[0]);
    TEST_ASSERT_EQUAL(128, led->rgb[1]);
    TEST_ASSERT_EQUAL(64, led->rgb[2]);
    TEST_ASSERT_EQUAL(50, led->brightness);

    // Field count should be 2 (rgb array + brightness)
    TEST_ASSERT_EQUAL(2, led.fieldCount());
}

// ==== is_wire_safe Tests ====

// Structs for wire-safe testing
struct UnsafeWithVector {
    std::vector<uint8_t> data;
    int32_t count;
};

struct UnsafeNested {
    Position pos;
    std::vector<int32_t> items;
};

void test_is_wire_safe_basic_types() {
    // Basic types should be wire-safe
    static_assert(is_wire_safe_v<bool>, "bool should be wire-safe");
    static_assert(is_wire_safe_v<int8_t>, "int8_t should be wire-safe");
    static_assert(is_wire_safe_v<uint8_t>, "uint8_t should be wire-safe");
    static_assert(is_wire_safe_v<int32_t>, "int32_t should be wire-safe");
    static_assert(is_wire_safe_v<float>, "float should be wire-safe");
    TEST_PASS();
}

void test_is_wire_safe_structs() {
    // Simple structs should be wire-safe
    static_assert(is_wire_safe_v<Position>, "Position should be wire-safe");
    static_assert(is_wire_safe_v<Position3D>, "Position3D should be wire-safe");
    static_assert(is_wire_safe_v<MixedData>, "MixedData should be wire-safe");
    static_assert(is_wire_safe_v<Color>, "Color should be wire-safe");
    static_assert(is_wire_safe_v<NestedStruct>, "NestedStruct should be wire-safe");
    TEST_PASS();
}

void test_is_wire_safe_array() {
    // std::array of wire-safe types should be wire-safe
    static_assert(is_wire_safe_v<std::array<uint8_t, 3>>, "array<uint8_t> should be wire-safe");
    static_assert(is_wire_safe_v<std::array<int32_t, 10>>, "array<int32_t> should be wire-safe");
    static_assert(is_wire_safe_v<WithArray>, "WithArray should be wire-safe");
    TEST_PASS();
}

void test_is_wire_safe_value_wrapper() {
    // Value<T> of wire-safe T should be wire-safe
    static_assert(is_wire_safe_v<Value<uint8_t>>, "Value<uint8_t> should be wire-safe");
    static_assert(is_wire_safe_v<Value<int32_t>>, "Value<int32_t> should be wire-safe");
    static_assert(is_wire_safe_v<ConfigWithFields>, "ConfigWithFields should be wire-safe");
    TEST_PASS();
}

void test_is_wire_safe_vector_not_safe() {
    // std::vector should NOT be wire-safe
    static_assert(!is_wire_safe_v<std::vector<uint8_t>>, "vector<uint8_t> should NOT be wire-safe");
    static_assert(!is_wire_safe_v<std::vector<int32_t>>, "vector<int32_t> should NOT be wire-safe");
    static_assert(!is_wire_safe_v<UnsafeWithVector>, "struct with vector should NOT be wire-safe");
    static_assert(!is_wire_safe_v<UnsafeNested>, "nested struct with vector should NOT be wire-safe");
    TEST_PASS();
}

void test_is_wire_safe_microlist_not_safe() {
    // MicroList should NOT be wire-safe (may use heap pointer)
    // Use MicroList with ListProperty instead, which handles serialization properly
    static_assert(!is_wire_safe_v<MicroList<uint8_t, 8>>, "MicroList<uint8_t> should NOT be wire-safe");
    static_assert(!is_wire_safe_v<MicroList<int32_t, 4>>, "MicroList<int32_t> should NOT be wire-safe");
    static_assert(!is_wire_safe_v<MicroList<uint8_t, 16, 256>>, "MicroList with max should NOT be wire-safe");

    // MicroList in a struct also makes the struct not wire-safe
    // (This would cause compile error if we tried to use it in ObjectProperty)

    // Verify is_micro_list_v trait
    static_assert(is_micro_list_v<MicroList<uint8_t, 8>>, "MicroList should be detected");
    static_assert(is_micro_list_v<MicroList<float, 4, 100>>, "MicroList with custom params should be detected");
    static_assert(!is_micro_list_v<std::vector<uint8_t>>, "std::vector should not be MicroList");
    static_assert(!is_micro_list_v<uint8_t>, "basic type should not be MicroList");
    static_assert(!is_micro_list_v<std::array<uint8_t, 8>>, "std::array should not be MicroList");

    // Type trait helpers
    static_assert(std::is_same_v<micro_list_element_t<MicroList<uint8_t, 8>>, uint8_t>,
        "Element type extraction should work");
    static_assert(micro_list_inline_capacity_v<MicroList<uint8_t, 16>> == 16,
        "Inline capacity extraction should work");
    static_assert(micro_list_max_capacity_v<MicroList<uint8_t, 8, 512>> == 512,
        "Max capacity extraction should work");

    TEST_PASS();
}

// ==== VariantProperty Tests ====

void test_variant_property_basic() {
    VariantProperty<2, 4> result("result",
        {
            VariantTypeDef("value", TYPE_UINT8, 1),
            VariantTypeDef("error", TYPE_INT32, 4)
        });

    TEST_ASSERT_EQUAL(TYPE_VARIANT, result.getTypeId());
    TEST_ASSERT_EQUAL(2, result.typeCount());
}

void test_variant_property_set_get() {
    VariantProperty<2, 4> result("result",
        {
            VariantTypeDef("value", TYPE_UINT8, 1),
            VariantTypeDef("error", TYPE_INT32, 4)
        });

    // Set to "value" type (index 0) - use uint8_t(0) to disambiguate
    TEST_ASSERT_TRUE(result.set<uint8_t>(uint8_t(0), 42));
    TEST_ASSERT_EQUAL(0, result.typeIndex());
    TEST_ASSERT_EQUAL(42, result.get<uint8_t>());

    // Set to "error" type (index 1)
    TEST_ASSERT_TRUE(result.set<int32_t>(uint8_t(1), -1));
    TEST_ASSERT_EQUAL(1, result.typeIndex());
    TEST_ASSERT_EQUAL(-1, result.get<int32_t>());
}

void test_variant_property_by_name() {
    VariantProperty<2, 4> result("result",
        {
            VariantTypeDef("success", TYPE_UINT8, 1),
            VariantTypeDef("failure", TYPE_INT32, 4)
        });

    // Set by name
    TEST_ASSERT_TRUE(result.set<uint8_t>("success", 100));
    TEST_ASSERT_TRUE(result.is("success"));
    TEST_ASSERT_FALSE(result.is("failure"));

    TEST_ASSERT_TRUE(result.set<int32_t>("failure", -500));
    TEST_ASSERT_FALSE(result.is("success"));
    TEST_ASSERT_TRUE(result.is("failure"));
    TEST_ASSERT_EQUAL(-500, result.get<int32_t>());
}

void test_variant_property_type_defs() {
    VariantProperty<3, 4> multi("multi",
        {
            VariantTypeDef("bool_val", TYPE_BOOL, 1),
            VariantTypeDef("int_val", TYPE_INT32, 4),
            VariantTypeDef("float_val", TYPE_FLOAT32, 4)
        });

    // Find by name
    TEST_ASSERT_EQUAL(0, multi.findType("bool_val"));
    TEST_ASSERT_EQUAL(1, multi.findType("int_val"));
    TEST_ASSERT_EQUAL(2, multi.findType("float_val"));
    TEST_ASSERT_EQUAL(3, multi.findType("unknown"));  // Not found

    // Check type defs
    const VariantTypeDef& def0 = multi.getTypeDef(0);
    TEST_ASSERT_EQUAL_STRING("bool_val", def0.name);
    TEST_ASSERT_EQUAL(TYPE_BOOL, def0.typeId);
    TEST_ASSERT_EQUAL(1, def0.size);
}

void test_variant_property_setdata() {
    VariantProperty<2, 4> result("result",
        {
            VariantTypeDef("value", TYPE_UINT8, 1),
            VariantTypeDef("code", TYPE_INT32, 4)
        });

    // Set via setData (type_index + value)
    uint8_t data1[] = { 0x00, 0x2A };  // type=0, value=42
    result.setData(data1, 2);
    TEST_ASSERT_EQUAL(0, result.typeIndex());
    TEST_ASSERT_EQUAL(42, result.get<uint8_t>());

    // Set to different type
    uint8_t data2[] = { 0x01, 0xFF, 0xFF, 0xFF, 0xFF };  // type=1, value=-1
    result.setData(data2, 5);
    TEST_ASSERT_EQUAL(1, result.typeIndex());
    TEST_ASSERT_EQUAL(-1, result.get<int32_t>());
}

// ==== ResourceProperty Tests ====

void test_resource_property_basic() {
    ResourceProperty<10, 32> resources("resources",
        ResourceTypeDef(TYPE_OBJECT, 32),
        ResourceTypeDef(TYPE_OBJECT, 0));

    TEST_ASSERT_EQUAL(TYPE_RESOURCE, resources.getTypeId());
    TEST_ASSERT_EQUAL(0, resources.resourceCount());
    TEST_ASSERT_EQUAL(10, resources.maxResources());
}

void test_resource_property_create() {
    ResourceProperty<5, 16> shaders("shaders",
        ResourceTypeDef(TYPE_OBJECT, 16),
        ResourceTypeDef(TYPE_OBJECT, 0));

    // Create a resource
    uint8_t header[16] = "test_shader";
    uint8_t body[32] = "void main() {}";

    uint32_t id = shaders.createResource(header, 16, body, 32);
    TEST_ASSERT_GREATER_THAN(0, id);
    TEST_ASSERT_EQUAL(1, shaders.resourceCount());

    // Verify header
    const ResourceHeader* hdr = shaders.getHeader(id);
    TEST_ASSERT_NOT_NULL(hdr);
    TEST_ASSERT_EQUAL(id, hdr->id);
    TEST_ASSERT_EQUAL(1, hdr->version);
    TEST_ASSERT_EQUAL(32, hdr->bodySize);
    TEST_ASSERT_TRUE(hdr->valid);
}

void test_resource_property_update_body() {
    ResourceProperty<5, 16> shaders("shaders",
        ResourceTypeDef(TYPE_OBJECT, 16),
        ResourceTypeDef(TYPE_OBJECT, 0));

    uint8_t header[16] = "shader1";
    uint8_t body1[20] = "version1";
    uint8_t body2[30] = "version2_updated";

    uint32_t id = shaders.createResource(header, 16, body1, 20);
    const ResourceHeader* hdr = shaders.getHeader(id);
    TEST_ASSERT_EQUAL(1, hdr->version);
    TEST_ASSERT_EQUAL(20, hdr->bodySize);

    // Update body
    TEST_ASSERT_TRUE(shaders.updateBody(id, body2, 30));
    TEST_ASSERT_EQUAL(2, hdr->version);  // Version incremented
    TEST_ASSERT_EQUAL(30, hdr->bodySize);

    // Read body (returns 0 in native test mode - no SPIFFS)
    uint8_t readBuf[64];
    size_t bytesRead = shaders.readBody(id, readBuf, sizeof(readBuf));
    // On native (no SPIFFS), readBody returns 0
    TEST_ASSERT_EQUAL(0, bytesRead);
}

void test_resource_property_delete() {
    ResourceProperty<5, 16> resources("resources",
        ResourceTypeDef(TYPE_OBJECT, 16),
        ResourceTypeDef(TYPE_OBJECT, 0));

    uint8_t header[16] = "test";
    uint8_t body[8] = "body";

    uint32_t id1 = resources.createResource(header, 16, body, 8);
    uint32_t id2 = resources.createResource(header, 16, body, 8);
    TEST_ASSERT_EQUAL(2, resources.resourceCount());

    // Delete first resource
    TEST_ASSERT_TRUE(resources.deleteResource(id1));
    TEST_ASSERT_EQUAL(1, resources.resourceCount());
    TEST_ASSERT_NULL(resources.getHeader(id1));
    TEST_ASSERT_NOT_NULL(resources.getHeader(id2));

    // Delete second resource
    TEST_ASSERT_TRUE(resources.deleteResource(id2));
    TEST_ASSERT_EQUAL(0, resources.resourceCount());

    // Delete non-existent
    TEST_ASSERT_FALSE(resources.deleteResource(999));
}

void test_resource_property_foreach() {
    ResourceProperty<5, 16> resources("resources",
        ResourceTypeDef(TYPE_OBJECT, 16),
        ResourceTypeDef(TYPE_OBJECT, 0));

    uint8_t h1[16] = "res1";
    uint8_t h2[16] = "res2";
    uint8_t h3[16] = "res3";
    uint8_t body[8] = "body";

    resources.createResource(h1, 16, body, 8);
    resources.createResource(h2, 16, body, 8);
    resources.createResource(h3, 16, body, 8);

    int count = 0;
    resources.forEach([&count](uint32_t id, const ResourceHeader& hdr, const void* data) {
        count++;
        return true;  // Continue iteration
    });

    TEST_ASSERT_EQUAL(3, count);
}

// ==== TypeCodec Encoding Tests ====

void test_object_encode() {
    ObjectProperty<Position> position("position");
    position->x = 100;
    position->y = 200;

    uint8_t buf[32];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(TypeCodec::encodeProperty(wb, &position));
    TEST_ASSERT_EQUAL(sizeof(Position), wb.position());

    // Verify little-endian encoding
    TEST_ASSERT_EQUAL_HEX8(0x64, buf[0]);  // 100
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0xC8, buf[4]);  // 200
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[5]);
}

void test_object_decode() {
    ObjectProperty<Position> position("position");

    uint8_t data[] = {
        0x64, 0x00, 0x00, 0x00,  // x = 100
        0xC8, 0x00, 0x00, 0x00   // y = 200
    };

    ReadBuffer rb(data, sizeof(data));
    TEST_ASSERT_TRUE(TypeCodec::decodeProperty(rb, &position));

    TEST_ASSERT_EQUAL(100, position->x);
    TEST_ASSERT_EQUAL(200, position->y);
}

// ==== Type Name Tests ====

void test_type_names() {
    TEST_ASSERT_EQUAL_STRING("OBJECT", TypeCodec::typeName(TYPE_OBJECT));
    TEST_ASSERT_EQUAL_STRING("VARIANT", TypeCodec::typeName(TYPE_VARIANT));
    TEST_ASSERT_EQUAL_STRING("RESOURCE", TypeCodec::typeName(TYPE_RESOURCE));
}

// ==== Field Names Tests ====

void test_field_names_registered() {
    // Position has field names registered
    TEST_ASSERT_TRUE(reflect::has_field_names_v<Position>);
    TEST_ASSERT_EQUAL_STRING("x", reflect::get_field_name<Position>(0));
    TEST_ASSERT_EQUAL_STRING("y", reflect::get_field_name<Position>(1));
    TEST_ASSERT_NULL(reflect::get_field_name<Position>(2));  // Out of bounds

    // MixedData has field names registered
    TEST_ASSERT_TRUE(reflect::has_field_names_v<MixedData>);
    TEST_ASSERT_EQUAL_STRING("flag", reflect::get_field_name<MixedData>(0));
    TEST_ASSERT_EQUAL_STRING("count", reflect::get_field_name<MixedData>(1));
    TEST_ASSERT_EQUAL_STRING("ratio", reflect::get_field_name<MixedData>(2));

    // Position3D does NOT have field names registered
    TEST_ASSERT_FALSE(reflect::has_field_names_v<Position3D>);
    TEST_ASSERT_NULL(reflect::get_field_name<Position3D>(0));
}

void test_object_schema_with_field_names() {
    // Test that OBJECT schema includes field names when registered
    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));

    // Encode Position schema (has field names)
    TEST_ASSERT_TRUE(SchemaTypeEncoder::encode<Position>(wb, nullptr));

    // Expected:
    // 0x22 = TYPE_OBJECT
    // 0x02 = field_count (2)
    // Field 0: 0x01 "x" + type definition (INT32 + no constraints)
    // Field 1: 0x01 "y" + type definition (INT32 + no constraints)
    size_t pos = 0;
    TEST_ASSERT_EQUAL_HEX8(TYPE_OBJECT, buf[pos++]);  // OBJECT type
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[pos++]);          // 2 fields (varint)

    // Field 0: "x"
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[pos++]);          // ident length = 1
    TEST_ASSERT_EQUAL('x', buf[pos++]);                 // field name "x"
    TEST_ASSERT_EQUAL_HEX8(TYPE_INT32, buf[pos++]);    // INT32 type
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[pos++]);          // no constraints

    // Field 1: "y"
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[pos++]);          // ident length = 1
    TEST_ASSERT_EQUAL('y', buf[pos++]);                 // field name "y"
    TEST_ASSERT_EQUAL_HEX8(TYPE_INT32, buf[pos++]);    // INT32 type
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[pos++]);          // no constraints

    TEST_ASSERT_EQUAL(pos, wb.position());
}

void test_object_schema_without_field_names() {
    // Test that OBJECT schema works without field names (uses empty ident)
    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));

    // Encode Position3D schema (no field names registered)
    TEST_ASSERT_TRUE(SchemaTypeEncoder::encode<Position3D>(wb, nullptr));

    // Expected:
    // 0x22 = TYPE_OBJECT
    // 0x03 = field_count (3)
    // Each field: 0x00 (empty ident) + type definition
    size_t pos = 0;
    TEST_ASSERT_EQUAL_HEX8(TYPE_OBJECT, buf[pos++]);  // OBJECT type
    TEST_ASSERT_EQUAL_HEX8(0x03, buf[pos++]);          // 3 fields (varint)

    // Field 0: empty name
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[pos++]);          // empty ident
    TEST_ASSERT_EQUAL_HEX8(TYPE_INT32, buf[pos++]);    // INT32 type
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[pos++]);          // no constraints

    // Field 1: empty name
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[pos++]);          // empty ident
    TEST_ASSERT_EQUAL_HEX8(TYPE_INT32, buf[pos++]);    // INT32 type
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[pos++]);          // no constraints

    // Field 2: empty name
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[pos++]);          // empty ident
    TEST_ASSERT_EQUAL_HEX8(TYPE_INT32, buf[pos++]);    // INT32 type
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[pos++]);          // no constraints

    TEST_ASSERT_EQUAL(pos, wb.position());
}

// ==== TypedResourceProperty Tests ====

// Define header and body types for testing
struct ShaderHeader {
    std::array<uint8_t, 16> name;
    bool enabled;
};
MICROPROTO_FIELD_NAMES(ShaderHeader, "name", "enabled");

struct ShaderBody {
    float speed;
    std::array<uint8_t, 3> color;
};
MICROPROTO_FIELD_NAMES(ShaderBody, "speed", "color");

void test_typed_resource_schema_encoding() {
    TypedResourceProperty<ShaderHeader, ShaderBody, 5> shaders("shaders");

    uint8_t buf[128];
    WriteBuffer wb(buf, sizeof(buf));

    // Encode the resource schema
    TEST_ASSERT_TRUE(shaders.encodeTypeDefinition(wb));

    // Verify structure:
    // TYPE_RESOURCE (0x24)
    // Header DATA_TYPE_DEFINITION: OBJECT with 2 fields (name: ARRAY, enabled: BOOL)
    // Body DATA_TYPE_DEFINITION: OBJECT with 2 fields (speed: FLOAT32, color: ARRAY)
    size_t pos = 0;
    TEST_ASSERT_EQUAL_HEX8(TYPE_RESOURCE, buf[pos++]);  // RESOURCE type

    // Header type: OBJECT
    TEST_ASSERT_EQUAL_HEX8(TYPE_OBJECT, buf[pos++]);    // OBJECT type
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[pos++]);           // 2 fields

    // Header field 0: "name" = ARRAY[16](UINT8)
    TEST_ASSERT_EQUAL_HEX8(0x04, buf[pos++]);           // ident length = 4
    TEST_ASSERT_EQUAL('n', buf[pos++]);
    TEST_ASSERT_EQUAL('a', buf[pos++]);
    TEST_ASSERT_EQUAL('m', buf[pos++]);
    TEST_ASSERT_EQUAL('e', buf[pos++]);
    TEST_ASSERT_EQUAL_HEX8(TYPE_ARRAY, buf[pos++]);     // ARRAY type
    TEST_ASSERT_EQUAL_HEX8(16, buf[pos++]);             // element_count = 16
    TEST_ASSERT_EQUAL_HEX8(TYPE_UINT8, buf[pos++]);     // element type = UINT8
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[pos++]);           // no constraints

    // Header field 1: "enabled" = BOOL
    TEST_ASSERT_EQUAL_HEX8(0x07, buf[pos++]);           // ident length = 7
    TEST_ASSERT_EQUAL('e', buf[pos++]);
    TEST_ASSERT_EQUAL('n', buf[pos++]);
    TEST_ASSERT_EQUAL('a', buf[pos++]);
    TEST_ASSERT_EQUAL('b', buf[pos++]);
    TEST_ASSERT_EQUAL('l', buf[pos++]);
    TEST_ASSERT_EQUAL('e', buf[pos++]);
    TEST_ASSERT_EQUAL('d', buf[pos++]);
    TEST_ASSERT_EQUAL_HEX8(TYPE_BOOL, buf[pos++]);      // BOOL type
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[pos++]);           // no constraints

    // Body type: OBJECT
    TEST_ASSERT_EQUAL_HEX8(TYPE_OBJECT, buf[pos++]);    // OBJECT type
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[pos++]);           // 2 fields

    // Body field 0: "speed" = FLOAT32
    TEST_ASSERT_EQUAL_HEX8(0x05, buf[pos++]);           // ident length = 5
    TEST_ASSERT_EQUAL('s', buf[pos++]);
    TEST_ASSERT_EQUAL('p', buf[pos++]);
    TEST_ASSERT_EQUAL('e', buf[pos++]);
    TEST_ASSERT_EQUAL('e', buf[pos++]);
    TEST_ASSERT_EQUAL('d', buf[pos++]);
    TEST_ASSERT_EQUAL_HEX8(TYPE_FLOAT32, buf[pos++]);   // FLOAT32 type
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[pos++]);           // no constraints

    // Body field 1: "color" = ARRAY[3](UINT8)
    TEST_ASSERT_EQUAL_HEX8(0x05, buf[pos++]);           // ident length = 5
    TEST_ASSERT_EQUAL('c', buf[pos++]);
    TEST_ASSERT_EQUAL('o', buf[pos++]);
    TEST_ASSERT_EQUAL('l', buf[pos++]);
    TEST_ASSERT_EQUAL('o', buf[pos++]);
    TEST_ASSERT_EQUAL('r', buf[pos++]);
    TEST_ASSERT_EQUAL_HEX8(TYPE_ARRAY, buf[pos++]);     // ARRAY type
    TEST_ASSERT_EQUAL_HEX8(3, buf[pos++]);              // element_count = 3
    TEST_ASSERT_EQUAL_HEX8(TYPE_UINT8, buf[pos++]);     // element type = UINT8
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[pos++]);           // no constraints

    TEST_ASSERT_EQUAL(pos, wb.position());
}

// ==== Main ====

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // ObjectProperty tests
    RUN_TEST(test_object_property_basic);
    RUN_TEST(test_object_property_field_access);
    RUN_TEST(test_object_property_get_field);
    RUN_TEST(test_object_property_initial_value);
    RUN_TEST(test_object_property_mixed_types);
    RUN_TEST(test_object_property_setdata);
    RUN_TEST(test_object_property_with_field_wrapper);
    RUN_TEST(test_object_property_foreach);
    RUN_TEST(test_object_property_nested);
    RUN_TEST(test_object_property_with_array);

    // is_wire_safe tests
    RUN_TEST(test_is_wire_safe_basic_types);
    RUN_TEST(test_is_wire_safe_structs);
    RUN_TEST(test_is_wire_safe_array);
    RUN_TEST(test_is_wire_safe_value_wrapper);
    RUN_TEST(test_is_wire_safe_vector_not_safe);
    RUN_TEST(test_is_wire_safe_microlist_not_safe);

    // VariantProperty tests
    RUN_TEST(test_variant_property_basic);
    RUN_TEST(test_variant_property_set_get);
    RUN_TEST(test_variant_property_by_name);
    RUN_TEST(test_variant_property_type_defs);
    RUN_TEST(test_variant_property_setdata);

    // ResourceProperty tests
    RUN_TEST(test_resource_property_basic);
    RUN_TEST(test_resource_property_create);
    RUN_TEST(test_resource_property_update_body);
    RUN_TEST(test_resource_property_delete);
    RUN_TEST(test_resource_property_foreach);

    // TypeCodec tests
    RUN_TEST(test_object_encode);
    RUN_TEST(test_object_decode);

    // Type name tests
    RUN_TEST(test_type_names);

    // Field names tests
    RUN_TEST(test_field_names_registered);
    RUN_TEST(test_object_schema_with_field_names);
    RUN_TEST(test_object_schema_without_field_names);

    // TypedResourceProperty tests
    RUN_TEST(test_typed_resource_schema_encoding);

    return UNITY_END();
}

#endif // NATIVE_TEST