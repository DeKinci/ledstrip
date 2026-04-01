#pragma once
#include <stdint.h>
#include <stddef.h>

namespace matter {

// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------
static constexpr uint16_t kPort = 5540;

// ---------------------------------------------------------------------------
// Protocol IDs
// ---------------------------------------------------------------------------
static constexpr uint16_t kProtoSecureChannel = 0x0000;
static constexpr uint16_t kProtoInteractionModel = 0x0001;

// ---------------------------------------------------------------------------
// Secure Channel Opcodes
// ---------------------------------------------------------------------------
static constexpr uint8_t kOpPBKDFParamRequest  = 0x20;
static constexpr uint8_t kOpPBKDFParamResponse = 0x21;
static constexpr uint8_t kOpPasePake1          = 0x22;
static constexpr uint8_t kOpPasePake2          = 0x23;
static constexpr uint8_t kOpPasePake3          = 0x24;
static constexpr uint8_t kOpCaseSigma1         = 0x30;
static constexpr uint8_t kOpCaseSigma2         = 0x31;
static constexpr uint8_t kOpCaseSigma3         = 0x32;
static constexpr uint8_t kOpStatusReport       = 0x40;

// ---------------------------------------------------------------------------
// Interaction Model Opcodes
// ---------------------------------------------------------------------------
static constexpr uint8_t kOpStatusResponse   = 0x01;
static constexpr uint8_t kOpReadRequest      = 0x02;
static constexpr uint8_t kOpSubscribeRequest = 0x03;
static constexpr uint8_t kOpSubscribeResponse= 0x04;
static constexpr uint8_t kOpReportData       = 0x05;
static constexpr uint8_t kOpWriteRequest     = 0x06;
static constexpr uint8_t kOpWriteResponse    = 0x07;
static constexpr uint8_t kOpInvokeRequest    = 0x08;
static constexpr uint8_t kOpInvokeResponse   = 0x09;
static constexpr uint8_t kOpTimedRequest     = 0x0A;

// ---------------------------------------------------------------------------
// Message Header Flags
// ---------------------------------------------------------------------------
static constexpr uint8_t kMsgFlagSBit       = 0x04; // Source Node ID present
static constexpr uint8_t kMsgFlagDSIZ_Mask  = 0x03;
static constexpr uint8_t kMsgFlagDSIZ_None  = 0x00;
static constexpr uint8_t kMsgFlagDSIZ_Node  = 0x01;
static constexpr uint8_t kMsgFlagDSIZ_Group = 0x02;

// Security Flags
static constexpr uint8_t kSecFlagPrivacy     = 0x80;
static constexpr uint8_t kSecFlagControl     = 0x40;
static constexpr uint8_t kSecFlagMsgExt      = 0x20;
static constexpr uint8_t kSecFlagSessionMask = 0x03;

// Exchange Flags
static constexpr uint8_t kExInitiator  = 0x01;
static constexpr uint8_t kExAck        = 0x02;
static constexpr uint8_t kExReliable   = 0x04;
static constexpr uint8_t kExVendor     = 0x10;

// ---------------------------------------------------------------------------
// Cluster IDs
// ---------------------------------------------------------------------------
static constexpr uint32_t kClusterDescriptor              = 0x001D;
static constexpr uint32_t kClusterBasicInfo                = 0x0028;
static constexpr uint32_t kClusterGeneralCommissioning     = 0x0030;
static constexpr uint32_t kClusterNetworkCommissioning     = 0x0031;
static constexpr uint32_t kClusterAdminCommissioning       = 0x003C;
static constexpr uint32_t kClusterOperationalCredentials   = 0x003E;
static constexpr uint32_t kClusterAccessControl             = 0x001F;
static constexpr uint32_t kClusterGroupKeyMgmt             = 0x003F;
static constexpr uint32_t kClusterGeneralDiagnostics       = 0x0033;
static constexpr uint32_t kClusterIdentify                  = 0x0003;
static constexpr uint32_t kClusterGroups                    = 0x0004;
static constexpr uint32_t kClusterOnOff                    = 0x0006;
static constexpr uint32_t kClusterLevelControl             = 0x0008;
static constexpr uint32_t kClusterColorControl             = 0x0300;

// ---------------------------------------------------------------------------
// OnOff Cluster
// ---------------------------------------------------------------------------
static constexpr uint32_t kAttrOnOff     = 0x0000;
static constexpr uint32_t kCmdOff        = 0x00;
static constexpr uint32_t kCmdOn         = 0x01;
static constexpr uint32_t kCmdToggle     = 0x02;

// ---------------------------------------------------------------------------
// LevelControl Cluster
// ---------------------------------------------------------------------------
static constexpr uint32_t kAttrCurrentLevel = 0x0000;
static constexpr uint32_t kAttrMinLevel     = 0x0002;
static constexpr uint32_t kAttrMaxLevel     = 0x0003;
static constexpr uint32_t kCmdMoveToLevel          = 0x00;
static constexpr uint32_t kCmdMoveToLevelWithOnOff = 0x04;

// ---------------------------------------------------------------------------
// ColorControl Cluster
// ---------------------------------------------------------------------------
static constexpr uint32_t kAttrCurrentHue        = 0x0000;
static constexpr uint32_t kAttrCurrentSaturation = 0x0001;
static constexpr uint32_t kAttrColorMode         = 0x0008;
static constexpr uint32_t kAttrColorCapabilities = 0x400A;
static constexpr uint32_t kAttrEnhancedColorMode = 0x4001;
static constexpr uint32_t kCmdMoveToHueSat       = 0x06;

// ---------------------------------------------------------------------------
// General Commissioning Cluster
// ---------------------------------------------------------------------------
static constexpr uint32_t kAttrBreadcrumb              = 0x0000;
static constexpr uint32_t kAttrBasicCommissioningInfo   = 0x0001;
static constexpr uint32_t kAttrRegulatoryConfig         = 0x0002;
static constexpr uint32_t kAttrLocationCapability       = 0x0003;
static constexpr uint32_t kCmdArmFailSafe               = 0x00;
static constexpr uint32_t kCmdArmFailSafeResp            = 0x01;
static constexpr uint32_t kCmdSetRegulatoryConfig        = 0x02;
static constexpr uint32_t kCmdSetRegulatoryConfigResp    = 0x03;
static constexpr uint32_t kCmdCommissioningComplete      = 0x04;
static constexpr uint32_t kCmdCommissioningCompleteResp  = 0x05;

// ---------------------------------------------------------------------------
// Operational Credentials Cluster
// ---------------------------------------------------------------------------
static constexpr uint32_t kCmdAttestationRequest   = 0x00;
static constexpr uint32_t kCmdAttestationResponse  = 0x01;
static constexpr uint32_t kCmdCertChainRequest     = 0x02;
static constexpr uint32_t kCmdCertChainResponse    = 0x03;
static constexpr uint32_t kCmdCSRRequest           = 0x04;
static constexpr uint32_t kCmdCSRResponse          = 0x05;
static constexpr uint32_t kCmdAddNOC               = 0x06;
static constexpr uint32_t kCmdNOCResponse          = 0x08;
static constexpr uint32_t kCmdAddTrustedRootCert   = 0x0B;
static constexpr uint32_t kAttrNOCs                = 0x0000;
static constexpr uint32_t kAttrFabrics             = 0x0001;
static constexpr uint32_t kAttrSupportedFabrics    = 0x0002;
static constexpr uint32_t kAttrCommissionedFabrics = 0x0003;
static constexpr uint32_t kAttrCurrentFabricIndex  = 0x0005;

// ---------------------------------------------------------------------------
// Descriptor Cluster
// ---------------------------------------------------------------------------
static constexpr uint32_t kAttrDeviceTypeList = 0x0000;
static constexpr uint32_t kAttrServerList     = 0x0001;
static constexpr uint32_t kAttrClientList     = 0x0002;
static constexpr uint32_t kAttrPartsList      = 0x0003;

// ---------------------------------------------------------------------------
// Basic Information Cluster
// ---------------------------------------------------------------------------
static constexpr uint32_t kAttrVendorName       = 0x0001;
static constexpr uint32_t kAttrVendorID         = 0x0002;
static constexpr uint32_t kAttrProductName      = 0x0003;
static constexpr uint32_t kAttrProductID        = 0x0004;
static constexpr uint32_t kAttrNodeLabel        = 0x0005;
static constexpr uint32_t kAttrHWVersion        = 0x0007;
static constexpr uint32_t kAttrHWVersionStr     = 0x0008;
static constexpr uint32_t kAttrSWVersion        = 0x0009;
static constexpr uint32_t kAttrSWVersionStr     = 0x000A;
static constexpr uint32_t kAttrSerialNumber     = 0x000F;
static constexpr uint32_t kAttrUniqueID         = 0x0012;
static constexpr uint32_t kAttrCapabilityMinima = 0x0013;

// ---------------------------------------------------------------------------
// Global Attributes (all clusters)
// ---------------------------------------------------------------------------
static constexpr uint32_t kAttrClusterRevision       = 0xFFFD;
static constexpr uint32_t kAttrFeatureMap             = 0xFFFC;
static constexpr uint32_t kAttrAttributeList          = 0xFFFB;
static constexpr uint32_t kAttrAcceptedCommandList    = 0xFFF9;
static constexpr uint32_t kAttrGeneratedCommandList   = 0xFFF8;

// ---------------------------------------------------------------------------
// Device Types
// ---------------------------------------------------------------------------
static constexpr uint32_t kDevTypeRootNode           = 0x0016;
static constexpr uint32_t kDevTypeExtendedColorLight = 0x010D;

// ---------------------------------------------------------------------------
// Status Codes
// ---------------------------------------------------------------------------
static constexpr uint8_t kStatusSuccess              = 0x00;
static constexpr uint8_t kStatusFailure              = 0x01;
static constexpr uint8_t kStatusUnsupportedAttribute = 0x86;
static constexpr uint8_t kStatusUnsupportedCommand   = 0x81;
static constexpr uint8_t kStatusUnsupportedCluster   = 0xC3;
static constexpr uint8_t kStatusConstraintError      = 0x87;

// StatusReport GeneralCode
static constexpr uint16_t kGeneralSuccess            = 0x0000;
static constexpr uint16_t kGeneralFailure            = 0x0001;
static constexpr uint16_t kGeneralBusy               = 0x0004;

// StatusReport ProtocolCode (Secure Channel)
static constexpr uint16_t kProtoCodeSessionEstablished = 0x0000;
static constexpr uint16_t kProtoCodeNoSharedRoot       = 0x0001;
static constexpr uint16_t kProtoCodeInvalidParam       = 0x0002;
static constexpr uint16_t kProtoCodeCloseSession       = 0x0003;

// ---------------------------------------------------------------------------
// Crypto Sizes
// ---------------------------------------------------------------------------
static constexpr size_t kP256PubKeySize      = 65;   // Uncompressed
static constexpr size_t kP256PrivKeySize     = 32;
static constexpr size_t kAES128KeySize       = 16;
static constexpr size_t kCCMNonceSize        = 13;
static constexpr size_t kCCMTagSize          = 16;
static constexpr size_t kSHA256Size          = 32;
static constexpr size_t kHMACSize            = 32;
static constexpr size_t kSPAKE2pPointSize    = 65;   // Uncompressed P-256

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------
static constexpr uint16_t kUnsecuredSessionId = 0;

// ---------------------------------------------------------------------------
// MRP (Message Reliability Protocol)
// ---------------------------------------------------------------------------
static constexpr uint8_t  kMRPMaxRetries      = 5;

// ---------------------------------------------------------------------------
// Test / Development Device Info
// ---------------------------------------------------------------------------
static constexpr uint16_t kTestVendorId    = 0xFFF1;
static constexpr uint16_t kTestProductId   = 0x8000;
static constexpr uint32_t kTestPasscode    = 20202021;
static constexpr uint16_t kTestDiscriminator = 3840;  // 0xF00

// PBKDF2 defaults
static constexpr uint32_t kPBKDF2Iterations = 1000;
static constexpr size_t   kPBKDF2SaltSize   = 32;

// ---------------------------------------------------------------------------
// Interaction Model
// ---------------------------------------------------------------------------
static constexpr uint8_t kInteractionModelRevision = 11; // Matter 1.3

// ---------------------------------------------------------------------------
// Matter message limits
// ---------------------------------------------------------------------------
static constexpr size_t kMaxMessageSize = 1280;
static constexpr size_t kMaxCertSize    = 600;
static constexpr size_t kMaxCSRSize     = 512;

} // namespace matter
