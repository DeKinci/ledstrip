#ifdef NATIVE_TEST

#include <unity.h>
#include <FunctionBase.h>
#include <messages/Schema.h>
#include <messages/MessageRouter.h>
#include <wire/Buffer.h>
#include <wire/OpCode.h>
#include <cstring>

using namespace MicroProto;

// =========== FunctionBase Registry Tests ===========

static void resetFunctions() {
    FunctionBase::resetRegistry();
}

void test_function_registration() {
    resetFunctions();

    static const ParamDef params[] = {{"slot", TYPE_UINT8}};
    FunctionBase func("savePreset", params, 1, TYPE_BOOL, "Save to slot");

    TEST_ASSERT_EQUAL(1, FunctionBase::count);
    TEST_ASSERT_EQUAL_STRING("savePreset", func.name);
    TEST_ASSERT_EQUAL(1, func.paramCount);
    TEST_ASSERT_EQUAL(TYPE_BOOL, func.returnTypeId);
    TEST_ASSERT_EQUAL_STRING("slot", func.params[0].name);
    TEST_ASSERT_EQUAL(TYPE_UINT8, func.params[0].typeId);
    TEST_ASSERT_TRUE(func.ble_exposed);
}

void test_function_no_params_no_return() {
    resetFunctions();

    FunctionBase func("reset");

    TEST_ASSERT_EQUAL(0, func.paramCount);
    TEST_ASSERT_EQUAL(TYPE_VOID, func.returnTypeId);
    TEST_ASSERT_NULL(func.params);
}

void test_function_handler_invocation() {
    resetFunctions();

    FunctionBase func("ping", nullptr, 0, TYPE_UINT8);
    func.onCall([](ReadBuffer& params, WriteBuffer& result) -> bool {
        result.writeByte(42);
        return true;
    });

    TEST_ASSERT_TRUE(func.hasHandler());

    uint8_t paramBuf[1] = {};
    ReadBuffer rb(paramBuf, 0);
    uint8_t resultBuf[16];
    WriteBuffer wb(resultBuf, sizeof(resultBuf));

    TEST_ASSERT_TRUE(func.invoke(rb, wb));
    TEST_ASSERT_EQUAL(1, wb.position());
    TEST_ASSERT_EQUAL(42, resultBuf[0]);
}

void test_function_handler_failure() {
    resetFunctions();

    FunctionBase func("fail");
    func.onCall([](ReadBuffer&, WriteBuffer&) -> bool {
        return false;
    });

    uint8_t paramBuf[1] = {};
    ReadBuffer rb(paramBuf, 0);
    uint8_t resultBuf[16];
    WriteBuffer wb(resultBuf, sizeof(resultBuf));

    TEST_ASSERT_FALSE(func.invoke(rb, wb));
}

void test_function_no_handler() {
    resetFunctions();

    FunctionBase func("empty");
    TEST_ASSERT_FALSE(func.hasHandler());

    uint8_t paramBuf[1] = {};
    ReadBuffer rb(paramBuf, 0);
    uint8_t resultBuf[16];
    WriteBuffer wb(resultBuf, sizeof(resultBuf));

    TEST_ASSERT_FALSE(func.invoke(rb, wb));
}

void test_function_handler_reads_params() {
    resetFunctions();

    static const ParamDef params[] = {{"a", TYPE_UINT8}, {"b", TYPE_UINT8}};
    FunctionBase func("add", params, 2, TYPE_UINT8);
    func.onCall([](ReadBuffer& p, WriteBuffer& r) -> bool {
        uint8_t a = p.readByte();
        uint8_t b = p.readByte();
        r.writeByte(a + b);
        return p.ok();
    });

    uint8_t paramBuf[] = {10, 32};
    ReadBuffer rb(paramBuf, sizeof(paramBuf));
    uint8_t resultBuf[16];
    WriteBuffer wb(resultBuf, sizeof(resultBuf));

    TEST_ASSERT_TRUE(func.invoke(rb, wb));
    TEST_ASSERT_EQUAL(42, resultBuf[0]);
}

void test_function_find() {
    resetFunctions();

    FunctionBase f1("func1");
    FunctionBase f2("func2");

    TEST_ASSERT_EQUAL(&f1, FunctionBase::find(f1.id));
    TEST_ASSERT_EQUAL(&f2, FunctionBase::find(f2.id));
    TEST_ASSERT_NULL(FunctionBase::find(99));
}

// =========== Function Schema Encoding Tests ===========

void test_function_schema_encode_simple() {
    resetFunctions();

    FunctionBase func("reset", nullptr, 0, TYPE_VOID, "Reset all");

    uint8_t buf[256];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(SchemaEncoder::encodeFunctionItem(wb, &func));
    TEST_ASSERT_GREATER_THAN(0, wb.position());

    // Verify: item_type = 0x02 (FUNCTION)
    ReadBuffer rb(buf, wb.position());
    uint8_t itemType = rb.readByte();
    TEST_ASSERT_EQUAL(0x02, itemType & 0x0F);

    // level_flags: ble_exposed = true
    uint8_t levelFlags = rb.readByte();
    TEST_ASSERT_TRUE(levelFlags & 0x04);

    // function_id
    uint16_t fid = rb.readPropId();
    TEST_ASSERT_EQUAL(func.id, fid);

    // namespace_id = 0
    uint16_t nsId = rb.readPropId();
    TEST_ASSERT_EQUAL(0, nsId);

    // name
    uint8_t nameLen = rb.readByte();
    TEST_ASSERT_EQUAL(5, nameLen);  // "reset"

    // Skip name bytes
    for (int i = 0; i < nameLen; i++) rb.readByte();

    // description (varint len + bytes)
    uint32_t descLen = rb.readVarint();
    TEST_ASSERT_EQUAL(9, descLen);  // "Reset all"

    // Skip desc bytes
    for (uint32_t i = 0; i < descLen; i++) rb.readByte();

    // param_count = 0
    uint8_t paramCount = rb.readByte();
    TEST_ASSERT_EQUAL(0, paramCount);

    // return type = TYPE_VOID (0x00)
    uint8_t retType = rb.readByte();
    TEST_ASSERT_EQUAL(TYPE_VOID, retType);
}

void test_function_schema_encode_with_params() {
    resetFunctions();

    static const ParamDef params[] = {{"slot", TYPE_UINT8}, {"name", TYPE_INT32}};
    FunctionBase func("save", params, 2, TYPE_BOOL);

    uint8_t buf[256];
    WriteBuffer wb(buf, sizeof(buf));

    TEST_ASSERT_TRUE(SchemaEncoder::encodeFunctionItem(wb, &func));

    ReadBuffer rb(buf, wb.position());

    // Skip item_type, level_flags, func_id, ns_id
    rb.readByte(); rb.readByte(); rb.readPropId(); rb.readPropId();

    // Skip name
    uint8_t nameLen = rb.readByte();
    for (int i = 0; i < nameLen; i++) rb.readByte();

    // Skip description (null → varint 0)
    uint32_t descLen = rb.readVarint();
    for (uint32_t i = 0; i < descLen; i++) rb.readByte();

    // param_count = 2
    uint8_t paramCount = rb.readByte();
    TEST_ASSERT_EQUAL(2, paramCount);

    // Param 0: "slot", TYPE_UINT8
    uint8_t p0Len = rb.readByte();
    TEST_ASSERT_EQUAL(4, p0Len);  // "slot"
    for (int i = 0; i < p0Len; i++) rb.readByte();
    TEST_ASSERT_EQUAL(TYPE_UINT8, rb.readByte());
    rb.readByte();  // validation_flags

    // Param 1: "name", TYPE_INT32
    uint8_t p1Len = rb.readByte();
    TEST_ASSERT_EQUAL(4, p1Len);  // "name"
    for (int i = 0; i < p1Len; i++) rb.readByte();
    TEST_ASSERT_EQUAL(TYPE_INT32, rb.readByte());
    rb.readByte();  // validation_flags

    // Return type: TYPE_BOOL + validation_flags
    TEST_ASSERT_EQUAL(TYPE_BOOL, rb.readByte());
    rb.readByte();  // validation_flags
}

void test_function_schema_batch_encode() {
    resetFunctions();

    FunctionBase f1("reset");
    FunctionBase f2("save");

    uint8_t buf[512];
    WriteBuffer wb(buf, sizeof(buf));

    size_t count = SchemaEncoder::encodeAllFunctions(wb);
    TEST_ASSERT_EQUAL(2, count);

    // Verify header: SCHEMA_UPSERT batched
    TEST_ASSERT_EQUAL_HEX8(0x13, buf[0]);  // opcode=3, flags=1 (batch)
    TEST_ASSERT_EQUAL(1, buf[1]);  // count-1 = 1 (meaning 2 items)
}

// =========== RPC Response Encoding Tests ===========

void test_rpc_response_success_with_value() {
    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));

    RpcFlags flags;
    flags.isResponse = true;
    flags.success = true;
    flags.hasReturnValue = true;

    wb.writeByte(encodeOpHeader(OpCode::RPC, flags.encode()));
    wb.writeByte(42);  // callId
    wb.writeByte(1);   // return value: true

    TEST_ASSERT_EQUAL_HEX8(0x75, buf[0]);  // opcode=5, flags=7 (is_response|success|has_return)
    TEST_ASSERT_EQUAL(42, buf[1]);
    TEST_ASSERT_EQUAL(1, buf[2]);
}

void test_rpc_response_success_no_value() {
    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));

    RpcFlags flags;
    flags.isResponse = true;
    flags.success = true;
    flags.hasReturnValue = false;

    wb.writeByte(encodeOpHeader(OpCode::RPC, flags.encode()));
    wb.writeByte(42);  // callId

    TEST_ASSERT_EQUAL_HEX8(0x35, buf[0]);  // opcode=5, flags=3 (is_response|success)
    TEST_ASSERT_EQUAL(42, buf[1]);
    TEST_ASSERT_EQUAL(2, wb.position());  // No return value bytes
}

void test_rpc_response_error() {
    uint8_t buf[128];
    WriteBuffer wb(buf, sizeof(buf));

    RpcFlags flags;
    flags.isResponse = true;
    flags.success = false;

    wb.writeByte(encodeOpHeader(OpCode::RPC, flags.encode()));
    wb.writeByte(42);  // callId
    wb.writeByte(0x03);  // errorCode
    wb.writeUtf8("Handler failed");

    TEST_ASSERT_EQUAL_HEX8(0x15, buf[0]);  // opcode=5, flags=1 (is_response only)
}

// =========== MessageRouter RPC Dispatch Tests ===========

// Mock MessageHandler that records RPC calls
struct MockRpcHandler : public MessageHandler {
    uint16_t lastFunctionId = 0;
    uint8_t lastCallId = 0;
    bool lastNeedsResponse = false;
    bool rpcCalled = false;
    uint8_t paramData[64] = {};
    size_t paramLen = 0;

    void onHello(uint8_t, const Hello&) override {}
    void onPropertyUpdate(uint8_t, uint16_t, const void*, size_t) override {}

    void onRpcRequest(uint8_t clientId, uint16_t functionId, uint8_t callId,
                      bool needsResponse, ReadBuffer& params) override {
        lastFunctionId = functionId;
        lastCallId = callId;
        lastNeedsResponse = needsResponse;
        rpcCalled = true;

        // Copy remaining params
        paramLen = 0;
        while (params.remaining() > 0 && paramLen < sizeof(paramData)) {
            paramData[paramLen++] = params.readByte();
        }
    }
};

void test_router_dispatches_rpc_request() {
    MockRpcHandler handler;
    MessageRouter router(&handler);

    // Build RPC request: opcode=5, needs_response=1, function_id=3, call_id=10, param=42
    uint8_t msg[16];
    WriteBuffer wb(msg, sizeof(msg));

    RpcFlags flags;
    flags.isResponse = false;
    flags.needsResponse = true;
    wb.writeByte(encodeOpHeader(OpCode::RPC, flags.encode()));
    wb.writePropId(3);
    wb.writeByte(10);  // callId
    wb.writeByte(42);  // param data

    TEST_ASSERT_TRUE(router.process(0, msg, wb.position()));
    TEST_ASSERT_TRUE(handler.rpcCalled);
    TEST_ASSERT_EQUAL(3, handler.lastFunctionId);
    TEST_ASSERT_EQUAL(10, handler.lastCallId);
    TEST_ASSERT_TRUE(handler.lastNeedsResponse);
    TEST_ASSERT_EQUAL(1, handler.paramLen);
    TEST_ASSERT_EQUAL(42, handler.paramData[0]);
}

void test_router_dispatches_fire_and_forget() {
    MockRpcHandler handler;
    MessageRouter router(&handler);

    uint8_t msg[16];
    WriteBuffer wb(msg, sizeof(msg));

    RpcFlags flags;
    flags.isResponse = false;
    flags.needsResponse = false;
    wb.writeByte(encodeOpHeader(OpCode::RPC, flags.encode()));
    wb.writePropId(5);  // function_id=5

    TEST_ASSERT_TRUE(router.process(0, msg, wb.position()));
    TEST_ASSERT_TRUE(handler.rpcCalled);
    TEST_ASSERT_EQUAL(5, handler.lastFunctionId);
    TEST_ASSERT_FALSE(handler.lastNeedsResponse);
    TEST_ASSERT_EQUAL(0, handler.lastCallId);
}

// =========== Edge Case Tests ===========

void test_function_ble_not_exposed() {
    resetFunctions();
    FunctionBase func("secret", nullptr, 0, TYPE_VOID, nullptr, false);

    TEST_ASSERT_FALSE(func.ble_exposed);
    TEST_ASSERT_NOT_NULL(FunctionBase::find(func.id));

    // encodeFunctionItem still works (WS server sends all), but ble_exposed bit is clear
    uint8_t buf[256];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(SchemaEncoder::encodeFunctionItem(wb, &func));
    TEST_ASSERT_EQUAL(0x00, buf[1] & 0x04);  // ble_exposed bit NOT set
}

void test_function_ble_filtered_batch() {
    // encodeAllFunctions encodes ALL functions (for WS server).
    // BLE server does its own filtering in sendSchema.
    // Verify that the batch count is correct when all are included.
    resetFunctions();

    FunctionBase f1("visible", nullptr, 0, TYPE_VOID, nullptr, true);
    FunctionBase f2("hidden", nullptr, 0, TYPE_VOID, nullptr, false);

    uint8_t buf[512];
    WriteBuffer wb(buf, sizeof(buf));
    size_t count = SchemaEncoder::encodeAllFunctions(wb);
    TEST_ASSERT_EQUAL(2, count);  // Both encoded (WS path)

    // Verify batch header says 2 items
    TEST_ASSERT_EQUAL(1, buf[1]);  // count-1 = 1 → 2 items
}

void test_function_null_description() {
    resetFunctions();
    FunctionBase func("nodesc", nullptr, 0, TYPE_VOID, nullptr);

    uint8_t buf[256];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(SchemaEncoder::encodeFunctionItem(wb, &func));

    // Should be decodeable — description is varint(0)
    ReadBuffer rb(buf, wb.position());
    rb.readByte();  // item_type
    rb.readByte();  // level_flags
    rb.readPropId(); // func_id
    rb.readPropId(); // ns_id

    // skip name
    uint8_t nameLen = rb.readByte();
    for (int i = 0; i < nameLen; i++) rb.readByte();

    // Description should be varint 0
    uint32_t descLen = rb.readVarint();
    TEST_ASSERT_EQUAL(0, descLen);
    TEST_ASSERT_TRUE(rb.ok());
}

void test_function_schema_roundtrip() {
    // Encode then decode: verify all fields survive
    resetFunctions();

    static const ParamDef params[] = {{"x", TYPE_INT32}, {"y", TYPE_FLOAT32}};
    FunctionBase func("move", params, 2, TYPE_BOOL, "Move to position");

    uint8_t buf[512];
    WriteBuffer wb(buf, sizeof(buf));
    TEST_ASSERT_TRUE(SchemaEncoder::encodeFunctionItem(wb, &func));

    ReadBuffer rb(buf, wb.position());

    // item_type = FUNCTION(2)
    TEST_ASSERT_EQUAL(2, rb.readByte() & 0x0F);
    // level_flags: ble_exposed
    TEST_ASSERT_TRUE(rb.readByte() & 0x04);
    // func id
    TEST_ASSERT_EQUAL(func.id, rb.readPropId());
    // ns id
    TEST_ASSERT_EQUAL(0, rb.readPropId());
    // name
    uint8_t nLen = rb.readByte();
    TEST_ASSERT_EQUAL(4, nLen);  // "move"
    char name[5] = {};
    for (int i = 0; i < nLen; i++) name[i] = rb.readByte();
    TEST_ASSERT_EQUAL_STRING("move", name);
    // description
    uint32_t dLen = rb.readVarint();
    TEST_ASSERT_EQUAL(16, dLen);  // "Move to position"
    for (uint32_t i = 0; i < dLen; i++) rb.readByte();
    // param_count
    TEST_ASSERT_EQUAL(2, rb.readByte());
    // param 0: "x", INT32
    TEST_ASSERT_EQUAL(1, rb.readByte());  // name len
    TEST_ASSERT_EQUAL('x', rb.readByte());
    TEST_ASSERT_EQUAL(TYPE_INT32, rb.readByte());
    rb.readByte();  // val flags
    // param 1: "y", FLOAT32
    TEST_ASSERT_EQUAL(1, rb.readByte());
    TEST_ASSERT_EQUAL('y', rb.readByte());
    TEST_ASSERT_EQUAL(TYPE_FLOAT32, rb.readByte());
    rb.readByte();  // val flags
    // return: BOOL
    TEST_ASSERT_EQUAL(TYPE_BOOL, rb.readByte());
    rb.readByte();  // val flags
    TEST_ASSERT_TRUE(rb.ok());
}

void test_encode_all_functions_empty() {
    resetFunctions();

    uint8_t buf[64];
    WriteBuffer wb(buf, sizeof(buf));
    size_t count = SchemaEncoder::encodeAllFunctions(wb);

    TEST_ASSERT_EQUAL(0, count);
    TEST_ASSERT_EQUAL(0, wb.position());
}

void test_fire_and_forget_ignores_result() {
    resetFunctions();

    FunctionBase func("noisy");
    func.onCall([](ReadBuffer&, WriteBuffer& r) -> bool {
        // Handler writes data even for fire-and-forget
        r.writeByte(99);
        r.writeByte(100);
        return true;
    });

    // Simulate fire-and-forget: needsResponse=false
    // The handler runs and writes to result, but nobody reads it — should not crash
    uint8_t paramBuf[1] = {};
    ReadBuffer rb(paramBuf, 0);
    uint8_t resultBuf[16];
    WriteBuffer wb(resultBuf, sizeof(resultBuf));

    TEST_ASSERT_TRUE(func.invoke(rb, wb));
    TEST_ASSERT_EQUAL(2, wb.position());  // Handler wrote, that's fine
}

void test_function_id_truncation_above_255() {
    // functionId comes as uint16_t from MessageRouter, but find() casts to uint8_t
    // IDs >= MAX_FUNCTIONS should return nullptr
    resetFunctions();
    FunctionBase func("only");

    TEST_ASSERT_NULL(FunctionBase::find(255));  // No function at 255
    TEST_ASSERT_NOT_NULL(FunctionBase::find(func.id));  // But func.id works
}

void test_router_rpc_with_multiple_param_bytes() {
    MockRpcHandler handler;
    MessageRouter router(&handler);

    uint8_t msg[32];
    WriteBuffer wb(msg, sizeof(msg));

    RpcFlags flags;
    flags.isResponse = false;
    flags.needsResponse = true;
    wb.writeByte(encodeOpHeader(OpCode::RPC, flags.encode()));
    wb.writePropId(1);
    wb.writeByte(7);  // callId
    // 4 bytes of param data (e.g. an int32)
    wb.writeByte(0x78);
    wb.writeByte(0x56);
    wb.writeByte(0x34);
    wb.writeByte(0x12);

    TEST_ASSERT_TRUE(router.process(0, msg, wb.position()));
    TEST_ASSERT_EQUAL(1, handler.lastFunctionId);
    TEST_ASSERT_EQUAL(7, handler.lastCallId);
    TEST_ASSERT_EQUAL(4, handler.paramLen);
    TEST_ASSERT_EQUAL(0x78, handler.paramData[0]);
    TEST_ASSERT_EQUAL(0x12, handler.paramData[3]);
}

void test_router_rpc_truncated_message() {
    MockRpcHandler handler;
    MessageRouter router(&handler);

    // Just the opcode byte, no function ID — should fail
    uint8_t msg[] = {0x25};  // RPC + needs_response

    TEST_ASSERT_FALSE(router.process(0, msg, sizeof(msg)));
    TEST_ASSERT_FALSE(handler.rpcCalled);
}

// =========== Test Runner ===========

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // FunctionBase registry
    RUN_TEST(test_function_registration);
    RUN_TEST(test_function_no_params_no_return);
    RUN_TEST(test_function_handler_invocation);
    RUN_TEST(test_function_handler_failure);
    RUN_TEST(test_function_no_handler);
    RUN_TEST(test_function_handler_reads_params);
    RUN_TEST(test_function_find);

    // Schema encoding
    RUN_TEST(test_function_schema_encode_simple);
    RUN_TEST(test_function_schema_encode_with_params);
    RUN_TEST(test_function_schema_batch_encode);

    // RPC response encoding
    RUN_TEST(test_rpc_response_success_with_value);
    RUN_TEST(test_rpc_response_success_no_value);
    RUN_TEST(test_rpc_response_error);

    // MessageRouter dispatch
    RUN_TEST(test_router_dispatches_rpc_request);
    RUN_TEST(test_router_dispatches_fire_and_forget);

    // Edge cases
    RUN_TEST(test_function_ble_not_exposed);
    RUN_TEST(test_function_ble_filtered_batch);
    RUN_TEST(test_function_null_description);
    RUN_TEST(test_function_schema_roundtrip);
    RUN_TEST(test_encode_all_functions_empty);
    RUN_TEST(test_fire_and_forget_ignores_result);
    RUN_TEST(test_function_id_truncation_above_255);
    RUN_TEST(test_router_rpc_with_multiple_param_bytes);
    RUN_TEST(test_router_rpc_truncated_message);

    return UNITY_END();
}

#endif // NATIVE_TEST
