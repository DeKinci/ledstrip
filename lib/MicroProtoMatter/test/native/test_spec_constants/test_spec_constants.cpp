// Matter Specification — Constant Validation Tests
// Verifies that all cluster IDs, attribute IDs, command IDs, opcodes,
// and protocol constants match the Matter Core Specification.

#include <unity.h>
#include "MatterConst.h"
#include "MatterMessage.h"

using namespace matter;

void setUp(void) {}
void tearDown(void) {}

// ============================================================================
// Protocol IDs (Spec 4.4.2)
// ============================================================================

void test_proto_secure_channel() { TEST_ASSERT_EQUAL(0x0000, kProtoSecureChannel); }
void test_proto_interaction_model() { TEST_ASSERT_EQUAL(0x0001, kProtoInteractionModel); }

// ============================================================================
// Secure Channel Opcodes (Spec 4.13)
// ============================================================================

void test_op_pbkdf_param_request() { TEST_ASSERT_EQUAL(0x20, kOpPBKDFParamRequest); }
void test_op_pbkdf_param_response() { TEST_ASSERT_EQUAL(0x21, kOpPBKDFParamResponse); }
void test_op_pase_pake1() { TEST_ASSERT_EQUAL(0x22, kOpPasePake1); }
void test_op_pase_pake2() { TEST_ASSERT_EQUAL(0x23, kOpPasePake2); }
void test_op_pase_pake3() { TEST_ASSERT_EQUAL(0x24, kOpPasePake3); }
void test_op_case_sigma1() { TEST_ASSERT_EQUAL(0x30, kOpCaseSigma1); }
void test_op_case_sigma2() { TEST_ASSERT_EQUAL(0x31, kOpCaseSigma2); }
void test_op_case_sigma3() { TEST_ASSERT_EQUAL(0x32, kOpCaseSigma3); }
void test_op_status_report() { TEST_ASSERT_EQUAL(0x40, kOpStatusReport); }

// ============================================================================
// Interaction Model Opcodes (Spec 8.2)
// ============================================================================

void test_op_status_response() { TEST_ASSERT_EQUAL(0x01, kOpStatusResponse); }
void test_op_read_request() { TEST_ASSERT_EQUAL(0x02, kOpReadRequest); }
void test_op_subscribe_request() { TEST_ASSERT_EQUAL(0x03, kOpSubscribeRequest); }
void test_op_subscribe_response() { TEST_ASSERT_EQUAL(0x04, kOpSubscribeResponse); }
void test_op_report_data() { TEST_ASSERT_EQUAL(0x05, kOpReportData); }
void test_op_write_request() { TEST_ASSERT_EQUAL(0x06, kOpWriteRequest); }
void test_op_write_response() { TEST_ASSERT_EQUAL(0x07, kOpWriteResponse); }
void test_op_invoke_request() { TEST_ASSERT_EQUAL(0x08, kOpInvokeRequest); }
void test_op_invoke_response() { TEST_ASSERT_EQUAL(0x09, kOpInvokeResponse); }
void test_op_timed_request() { TEST_ASSERT_EQUAL(0x0A, kOpTimedRequest); }

// ============================================================================
// Cluster IDs (Spec various chapters)
// ============================================================================

void test_cluster_descriptor() { TEST_ASSERT_EQUAL(0x001D, kClusterDescriptor); }
void test_cluster_basic_info() { TEST_ASSERT_EQUAL(0x0028, kClusterBasicInfo); }
void test_cluster_general_commissioning() { TEST_ASSERT_EQUAL(0x0030, kClusterGeneralCommissioning); }
void test_cluster_network_commissioning() { TEST_ASSERT_EQUAL(0x0031, kClusterNetworkCommissioning); }
void test_cluster_admin_commissioning() { TEST_ASSERT_EQUAL(0x003C, kClusterAdminCommissioning); }
void test_cluster_operational_credentials() { TEST_ASSERT_EQUAL(0x003E, kClusterOperationalCredentials); }
void test_cluster_on_off() { TEST_ASSERT_EQUAL(0x0006, kClusterOnOff); }
void test_cluster_level_control() { TEST_ASSERT_EQUAL(0x0008, kClusterLevelControl); }
void test_cluster_color_control() { TEST_ASSERT_EQUAL(0x0300, kClusterColorControl); }
void test_cluster_identify() { TEST_ASSERT_EQUAL(0x0003, kClusterIdentify); }
void test_cluster_groups() { TEST_ASSERT_EQUAL(0x0004, kClusterGroups); }

// ============================================================================
// OnOff Cluster (Spec 1.5)
// ============================================================================

void test_onoff_attr_onoff() { TEST_ASSERT_EQUAL(0x0000, kAttrOnOff); }
void test_onoff_cmd_off() { TEST_ASSERT_EQUAL(0x00, kCmdOff); }
void test_onoff_cmd_on() { TEST_ASSERT_EQUAL(0x01, kCmdOn); }
void test_onoff_cmd_toggle() { TEST_ASSERT_EQUAL(0x02, kCmdToggle); }

// ============================================================================
// LevelControl Cluster (Spec 1.6)
// ============================================================================

void test_level_attr_current() { TEST_ASSERT_EQUAL(0x0000, kAttrCurrentLevel); }
void test_level_attr_min() { TEST_ASSERT_EQUAL(0x0002, kAttrMinLevel); }
void test_level_attr_max() { TEST_ASSERT_EQUAL(0x0003, kAttrMaxLevel); }
void test_level_cmd_move_to() { TEST_ASSERT_EQUAL(0x00, kCmdMoveToLevel); }
void test_level_cmd_move_to_with_onoff() { TEST_ASSERT_EQUAL(0x04, kCmdMoveToLevelWithOnOff); }

// ============================================================================
// ColorControl Cluster (Spec 3.2)
// ============================================================================

void test_color_attr_hue() { TEST_ASSERT_EQUAL(0x0000, kAttrCurrentHue); }
void test_color_attr_saturation() { TEST_ASSERT_EQUAL(0x0001, kAttrCurrentSaturation); }
void test_color_attr_mode() { TEST_ASSERT_EQUAL(0x0008, kAttrColorMode); }
void test_color_cmd_move_to_hue_sat() { TEST_ASSERT_EQUAL(0x06, kCmdMoveToHueSat); }

// ============================================================================
// Message Header Flags (Spec 4.4.1)
// ============================================================================

void test_flag_s_bit() { TEST_ASSERT_EQUAL(0x04, kMsgFlagSBit); }
void test_flag_dsiz_none() { TEST_ASSERT_EQUAL(0x00, kMsgFlagDSIZ_None); }
void test_flag_dsiz_node() { TEST_ASSERT_EQUAL(0x01, kMsgFlagDSIZ_Node); }
void test_flag_dsiz_group() { TEST_ASSERT_EQUAL(0x02, kMsgFlagDSIZ_Group); }

// ============================================================================
// Security Flags (Spec 4.4.1.2)
// ============================================================================

void test_secflag_privacy() { TEST_ASSERT_EQUAL(0x80, kSecFlagPrivacy); }
void test_secflag_control() { TEST_ASSERT_EQUAL(0x40, kSecFlagControl); }
void test_secflag_msg_ext() { TEST_ASSERT_EQUAL(0x20, kSecFlagMsgExt); }
void test_secflag_session_mask() { TEST_ASSERT_EQUAL(0x03, kSecFlagSessionMask); }

// ============================================================================
// Exchange Flags (Spec 4.4.3)
// ============================================================================

void test_exflag_initiator() { TEST_ASSERT_EQUAL(0x01, kExInitiator); }
void test_exflag_ack() { TEST_ASSERT_EQUAL(0x02, kExAck); }
void test_exflag_reliable() { TEST_ASSERT_EQUAL(0x04, kExReliable); }
void test_exflag_vendor() { TEST_ASSERT_EQUAL(0x10, kExVendor); }

// ============================================================================
// Crypto Constants (Spec various)
// ============================================================================

void test_port() { TEST_ASSERT_EQUAL(5540, kPort); }
void test_ccm_tag_size() { TEST_ASSERT_EQUAL(16, kCCMTagSize); }
void test_p256_pub_key_size() { TEST_ASSERT_EQUAL(65, kP256PubKeySize); }
void test_p256_priv_key_size() { TEST_ASSERT_EQUAL(32, kP256PrivKeySize); }
void test_aes128_key_size() { TEST_ASSERT_EQUAL(16, kAES128KeySize); }
void test_sha256_size() { TEST_ASSERT_EQUAL(32, kSHA256Size); }

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // Protocol IDs
    RUN_TEST(test_proto_secure_channel);
    RUN_TEST(test_proto_interaction_model);

    // Secure Channel Opcodes
    RUN_TEST(test_op_pbkdf_param_request);
    RUN_TEST(test_op_pbkdf_param_response);
    RUN_TEST(test_op_pase_pake1);
    RUN_TEST(test_op_pase_pake2);
    RUN_TEST(test_op_pase_pake3);
    RUN_TEST(test_op_case_sigma1);
    RUN_TEST(test_op_case_sigma2);
    RUN_TEST(test_op_case_sigma3);
    RUN_TEST(test_op_status_report);

    // IM Opcodes
    RUN_TEST(test_op_status_response);
    RUN_TEST(test_op_read_request);
    RUN_TEST(test_op_subscribe_request);
    RUN_TEST(test_op_subscribe_response);
    RUN_TEST(test_op_report_data);
    RUN_TEST(test_op_write_request);
    RUN_TEST(test_op_write_response);
    RUN_TEST(test_op_invoke_request);
    RUN_TEST(test_op_invoke_response);
    RUN_TEST(test_op_timed_request);

    // Cluster IDs
    RUN_TEST(test_cluster_descriptor);
    RUN_TEST(test_cluster_basic_info);
    RUN_TEST(test_cluster_general_commissioning);
    RUN_TEST(test_cluster_network_commissioning);
    RUN_TEST(test_cluster_admin_commissioning);
    RUN_TEST(test_cluster_operational_credentials);
    RUN_TEST(test_cluster_on_off);
    RUN_TEST(test_cluster_level_control);
    RUN_TEST(test_cluster_color_control);
    RUN_TEST(test_cluster_identify);
    RUN_TEST(test_cluster_groups);

    // OnOff
    RUN_TEST(test_onoff_attr_onoff);
    RUN_TEST(test_onoff_cmd_off);
    RUN_TEST(test_onoff_cmd_on);
    RUN_TEST(test_onoff_cmd_toggle);

    // LevelControl
    RUN_TEST(test_level_attr_current);
    RUN_TEST(test_level_attr_min);
    RUN_TEST(test_level_attr_max);
    RUN_TEST(test_level_cmd_move_to);
    RUN_TEST(test_level_cmd_move_to_with_onoff);

    // ColorControl
    RUN_TEST(test_color_attr_hue);
    RUN_TEST(test_color_attr_saturation);
    RUN_TEST(test_color_attr_mode);
    RUN_TEST(test_color_cmd_move_to_hue_sat);

    // Header flags
    RUN_TEST(test_flag_s_bit);
    RUN_TEST(test_flag_dsiz_none);
    RUN_TEST(test_flag_dsiz_node);
    RUN_TEST(test_flag_dsiz_group);

    // Security flags
    RUN_TEST(test_secflag_privacy);
    RUN_TEST(test_secflag_control);
    RUN_TEST(test_secflag_msg_ext);
    RUN_TEST(test_secflag_session_mask);

    // Exchange flags
    RUN_TEST(test_exflag_initiator);
    RUN_TEST(test_exflag_ack);
    RUN_TEST(test_exflag_reliable);
    RUN_TEST(test_exflag_vendor);

    // Crypto constants
    RUN_TEST(test_port);
    RUN_TEST(test_ccm_tag_size);
    RUN_TEST(test_p256_pub_key_size);
    RUN_TEST(test_p256_priv_key_size);
    RUN_TEST(test_aes128_key_size);
    RUN_TEST(test_sha256_size);

    return UNITY_END();
}
