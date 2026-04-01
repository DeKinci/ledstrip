// Matter Interaction Model TLV structure tests
// Reference: Matter Spec Section 8 — Interaction Model
// Validates TLV encoding of IM messages matches spec structure

#include <unity.h>
#include "MatterTLV.h"
#include "MatterConst.h"

using namespace matter;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// ReadRequest (Spec 8.4.2)
// Structure: {
//   0: AttributeRequests[] = [ AttributePathIB, ... ]
//   3: FabricFiltered (bool)
// }
// AttributePathIB = list { 2: EndpointId, 3: ClusterId, 4: AttributeId }
// ============================================================================

void test_im_read_request_single_attribute() {
    uint8_t buf[64];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
      w.openArray(0);           // AttributeRequests
        w.openList(kAnon);      // AttributePathIB
          w.putU16(2, 1);       // Endpoint 1
          w.putU32(3, kClusterOnOff);
          w.putU32(4, kAttrOnOff);
        w.closeContainer();
      w.closeContainer();
      w.putBool(3, true);       // FabricFiltered
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());

    // Parse and verify structure
    TLVReader r(buf, w.size());
    r.next(); TEST_ASSERT_TRUE(r.isStruct());
    r.next(); TEST_ASSERT_EQUAL(0, r.tag()); TEST_ASSERT_TRUE(r.isArray());
    r.next(); TEST_ASSERT_TRUE(r.isList()); // AttributePathIB
    r.next(); TEST_ASSERT_EQUAL(2, r.tag()); TEST_ASSERT_EQUAL(1, r.getU16());
    r.next(); TEST_ASSERT_EQUAL(3, r.tag()); TEST_ASSERT_EQUAL(kClusterOnOff, r.getU32());
    r.next(); TEST_ASSERT_EQUAL(4, r.tag()); TEST_ASSERT_EQUAL(kAttrOnOff, r.getU32());
    r.next(); TEST_ASSERT_TRUE(r.isEnd()); // list end
    r.next(); TEST_ASSERT_TRUE(r.isEnd()); // array end
    r.next(); TEST_ASSERT_EQUAL(3, r.tag()); TEST_ASSERT_TRUE(r.getBool());
}

void test_im_read_request_multiple_attributes() {
    uint8_t buf[128];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
      w.openArray(0);
        // Path 1: OnOff
        w.openList(kAnon);
          w.putU16(2, 1);
          w.putU32(3, kClusterOnOff);
          w.putU32(4, kAttrOnOff);
        w.closeContainer();
        // Path 2: LevelControl
        w.openList(kAnon);
          w.putU16(2, 1);
          w.putU32(3, kClusterLevelControl);
          w.putU32(4, kAttrCurrentLevel);
        w.closeContainer();
      w.closeContainer();
      w.putBool(3, true);
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());
}

void test_im_read_request_wildcard_endpoint() {
    // Omitting endpoint means wildcard (all endpoints)
    uint8_t buf[64];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
      w.openArray(0);
        w.openList(kAnon);
          // No endpoint tag — wildcard
          w.putU32(3, kClusterBasicInfo);
          w.putU32(4, 0xFFFD); // ClusterRevision
        w.closeContainer();
      w.closeContainer();
      w.putBool(3, false);
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());

    TLVReader r(buf, w.size());
    r.next(); // struct
    r.next(); // array
    r.next(); // list (path)
    r.next(); // first field — should be tag 3 (cluster), not tag 2 (endpoint)
    TEST_ASSERT_EQUAL(3, r.tag());
}

// ============================================================================
// ReportData (Spec 8.4.4)
// Structure: {
//   1: AttributeReports[] = [
//     { 1: AttributeDataIB = {
//         0: DataVersion (u32),
//         1: AttributePathIB,
//         2: Data (any)
//     }}
//   ]
// }
// ============================================================================

void test_im_report_data_single() {
    uint8_t buf[96];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
      w.openArray(1);           // AttributeReports
        w.openStruct(kAnon);    // AttributeReportIB
          w.openStruct(1);      // AttributeDataIB
            w.putU32(0, 1);     // DataVersion
            w.openList(1);      // AttributePathIB
              w.putU16(2, 1);
              w.putU32(3, kClusterOnOff);
              w.putU32(4, kAttrOnOff);
            w.closeContainer();
            w.putBool(2, true); // Data: OnOff = true
          w.closeContainer();
        w.closeContainer();
      w.closeContainer();
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());
}

// ============================================================================
// WriteRequest (Spec 8.6.2)
// Structure: {
//   0: SuppressResponse (bool),
//   1: TimedRequest (bool),
//   2: WriteRequests[] = [
//     { 1: AttributePathIB, 2: Data }
//   ]
// }
// ============================================================================

void test_im_write_request() {
    uint8_t buf[96];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
      w.putBool(0, false);      // SuppressResponse
      w.putBool(1, false);      // TimedRequest
      w.openArray(2);           // WriteRequests
        w.openStruct(kAnon);    // AttributeDataIB
          w.openList(1);        // Path
            w.putU16(2, 1);
            w.putU32(3, kClusterLevelControl);
            w.putU32(4, kAttrCurrentLevel);
          w.closeContainer();
          w.putU8(2, 128);      // Data: level = 128
        w.closeContainer();
      w.closeContainer();
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());
}

// ============================================================================
// InvokeRequest (Spec 8.8.2)
// Structure: {
//   0: SuppressResponse (bool),
//   1: TimedRequest (bool),
//   2: InvokeRequests[] = [
//     { 0: CommandPathIB = list { 0: endpoint, 1: cluster, 2: command },
//       1: CommandFields (optional) }
//   ]
// }
// ============================================================================

void test_im_invoke_request_no_fields() {
    // Toggle command — no fields
    uint8_t buf[64];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
      w.putBool(0, false);
      w.putBool(1, false);
      w.openArray(2);
        w.openStruct(kAnon);   // CommandDataIB
          w.openList(0);       // CommandPathIB
            w.putU16(0, 1);    // Endpoint
            w.putU32(1, kClusterOnOff);
            w.putU32(2, kCmdToggle);
          w.closeContainer();
          // No CommandFields for Toggle
        w.closeContainer();
      w.closeContainer();
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());
}

void test_im_invoke_request_with_fields() {
    // MoveToLevel with level=200, transitionTime=10
    uint8_t buf[96];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
      w.putBool(0, false);
      w.putBool(1, false);
      w.openArray(2);
        w.openStruct(kAnon);
          w.openList(0);       // CommandPathIB
            w.putU16(0, 1);
            w.putU32(1, kClusterLevelControl);
            w.putU32(2, kCmdMoveToLevel);
          w.closeContainer();
          w.openStruct(1);     // CommandFields
            w.putU8(0, 200);   // Level
            w.putU16(1, 10);   // TransitionTime (tenths of second)
            w.putU8(2, 0);     // OptionsMask
            w.putU8(3, 0);     // OptionsOverride
          w.closeContainer();
        w.closeContainer();
      w.closeContainer();
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());

    // Verify fields parse correctly
    TLVReader r(buf, w.size());
    r.next(); // outer struct
    r.next(); r.next(); // suppress, timed
    r.next(); // array
    r.next(); // CommandDataIB
    r.next(); // CommandPathIB (list)
    r.skipContainer(); // skip path
    r.next(); // CommandFields (struct)
    TEST_ASSERT_EQUAL(1, r.tag());
    TEST_ASSERT_TRUE(r.isStruct());
    r.next(); TEST_ASSERT_EQUAL(0, r.tag()); TEST_ASSERT_EQUAL(200, r.getU8());
    r.next(); TEST_ASSERT_EQUAL(1, r.tag()); TEST_ASSERT_EQUAL(10, r.getU16());
}

// ============================================================================
// InvokeResponse (Spec 8.8.4)
// Structure: {
//   0: SuppressResponse (bool),
//   1: InvokeResponses[] = [
//     { 0: CommandDataIB | 1: CommandStatusIB }
//   ]
// }
// CommandStatusIB = { 0: CommandPathIB, 1: StatusIB = { 0: Status, 1: ClusterStatus } }
// ============================================================================

void test_im_invoke_response_success() {
    uint8_t buf[64];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
      w.putBool(0, false);
      w.openArray(1);           // InvokeResponses
        w.openStruct(kAnon);
          w.openStruct(1);      // CommandStatusIB
            w.openList(0);      // CommandPathIB
              w.putU16(0, 1);
              w.putU32(1, kClusterOnOff);
              w.putU32(2, kCmdToggle);
            w.closeContainer();
            w.openStruct(1);    // StatusIB
              w.putU8(0, 0);    // Status = SUCCESS
            w.closeContainer();
          w.closeContainer();
        w.closeContainer();
      w.closeContainer();
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());
}

// ============================================================================
// SubscribeRequest (Spec 8.5.2)
// Structure: {
//   0: KeepSubscriptions (bool),
//   1: MinIntervalFloor (u16),
//   2: MaxIntervalCeiling (u16),
//   3: AttributeRequests[],
//   4: EventRequests[] (optional),
//   5: FabricFiltered (bool)
// }
// ============================================================================

void test_im_subscribe_request() {
    uint8_t buf[96];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
      w.putBool(0, false);      // KeepSubscriptions
      w.putU16(1, 0);           // MinIntervalFloor
      w.putU16(2, 60);          // MaxIntervalCeiling
      w.openArray(3);           // AttributeRequests
        w.openList(kAnon);
          w.putU16(2, 1);
          w.putU32(3, kClusterOnOff);
          w.putU32(4, kAttrOnOff);
        w.closeContainer();
      w.closeContainer();
      w.putBool(5, true);       // FabricFiltered
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());
}

// ============================================================================
// Global Attributes (Spec 7.13)
// ============================================================================

void test_im_global_attribute_cluster_revision() {
    // ClusterRevision (0xFFFD) should be readable on all clusters
    uint8_t buf[64];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
      w.openArray(0);
        w.openList(kAnon);
          w.putU16(2, 0);       // Endpoint 0
          w.putU32(3, kClusterBasicInfo);
          w.putU32(4, 0xFFFD);  // ClusterRevision
        w.closeContainer();
      w.closeContainer();
      w.putBool(3, false);
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());
}

void test_im_global_attribute_feature_map() {
    uint8_t buf[64];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
      w.openArray(0);
        w.openList(kAnon);
          w.putU16(2, 0);
          w.putU32(3, kClusterOnOff);
          w.putU32(4, 0xFFFC);  // FeatureMap
        w.closeContainer();
      w.closeContainer();
      w.putBool(3, false);
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());
}

void test_im_global_attribute_attribute_list() {
    uint8_t buf[64];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
      w.openArray(0);
        w.openList(kAnon);
          w.putU16(2, 1);
          w.putU32(3, kClusterOnOff);
          w.putU32(4, 0xFFFB);  // AttributeList
        w.closeContainer();
      w.closeContainer();
      w.putBool(3, false);
    w.closeContainer();
    TEST_ASSERT_FALSE(w.error());
}

// ============================================================================
// StatusIB (Spec 8.9.7)
// ============================================================================

void test_im_status_ib_success() {
    uint8_t buf[16];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
      w.putU8(0, 0x00); // Status: SUCCESS
    w.closeContainer();

    TLVReader r(buf, w.size());
    r.next(); r.next();
    TEST_ASSERT_EQUAL(0, r.tag());
    TEST_ASSERT_EQUAL(0x00, r.getU8());
}

void test_im_status_ib_unsupported_attribute() {
    uint8_t buf[16];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
      w.putU8(0, 0x86); // UNSUPPORTED_ATTRIBUTE
    w.closeContainer();

    TLVReader r(buf, w.size());
    r.next(); r.next();
    TEST_ASSERT_EQUAL(0x86, r.getU8());
}

void test_im_status_ib_unsupported_cluster() {
    uint8_t buf[16];
    TLVWriter w(buf, sizeof(buf));
    w.openStruct(kAnon);
      w.putU8(0, 0xC3); // UNSUPPORTED_CLUSTER
    w.closeContainer();

    TLVReader r(buf, w.size());
    r.next(); r.next();
    TEST_ASSERT_EQUAL(0xC3, r.getU8());
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // ReadRequest
    RUN_TEST(test_im_read_request_single_attribute);
    RUN_TEST(test_im_read_request_multiple_attributes);
    RUN_TEST(test_im_read_request_wildcard_endpoint);

    // ReportData
    RUN_TEST(test_im_report_data_single);

    // WriteRequest
    RUN_TEST(test_im_write_request);

    // InvokeRequest
    RUN_TEST(test_im_invoke_request_no_fields);
    RUN_TEST(test_im_invoke_request_with_fields);

    // InvokeResponse
    RUN_TEST(test_im_invoke_response_success);

    // SubscribeRequest
    RUN_TEST(test_im_subscribe_request);

    // Global attributes
    RUN_TEST(test_im_global_attribute_cluster_revision);
    RUN_TEST(test_im_global_attribute_feature_map);
    RUN_TEST(test_im_global_attribute_attribute_list);

    // StatusIB
    RUN_TEST(test_im_status_ib_success);
    RUN_TEST(test_im_status_ib_unsupported_attribute);
    RUN_TEST(test_im_status_ib_unsupported_cluster);

    return UNITY_END();
}
