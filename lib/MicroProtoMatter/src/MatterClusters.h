#pragma once
#include <stdint.h>
#include <math.h>
#include "MatterConst.h"
#include "MatterTLV.h"
#include "PropertyBase.h"
#include "Property.h"
#include "ArrayProperty.h"

namespace matter {

// ---------------------------------------------------------------------------
// Color conversion helpers
//
// Matter Hue: 0-254 maps to 0-360 degrees
// Matter Saturation: 0-254
// Matter Level: 0-254 (brightness)
// ---------------------------------------------------------------------------

inline void rgbToHueSat(uint8_t r, uint8_t g, uint8_t b,
                         uint8_t& hue, uint8_t& sat) {
    float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
    float mx = rf > gf ? (rf > bf ? rf : bf) : (gf > bf ? gf : bf);
    float mn = rf < gf ? (rf < bf ? rf : bf) : (gf < bf ? gf : bf);
    float delta = mx - mn;

    sat = (mx == 0) ? 0 : (uint8_t)(delta / mx * 254.0f);

    float h = 0;
    if (delta > 0.001f) {
        if (mx == rf)      h = 60.0f * fmodf((gf - bf) / delta, 6.0f);
        else if (mx == gf) h = 60.0f * ((bf - rf) / delta + 2.0f);
        else               h = 60.0f * ((rf - gf) / delta + 4.0f);
    }
    if (h < 0) h += 360.0f;
    hue = (uint8_t)(h / 360.0f * 254.0f);
}

inline void hueSatToRgb(uint8_t hue, uint8_t sat, uint8_t level,
                         uint8_t& r, uint8_t& g, uint8_t& b) {
    float h = hue / 254.0f * 360.0f;
    float s = sat / 254.0f;
    float v = level / 254.0f;
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rf, gf, bf;
    if      (h < 60)  { rf = c; gf = x; bf = 0; }
    else if (h < 120) { rf = x; gf = c; bf = 0; }
    else if (h < 180) { rf = 0; gf = c; bf = x; }
    else if (h < 240) { rf = 0; gf = x; bf = c; }
    else if (h < 300) { rf = x; gf = 0; bf = c; }
    else              { rf = c; gf = 0; bf = x; }
    r = (uint8_t)((rf + m) * 255.0f);
    g = (uint8_t)((gf + m) * 255.0f);
    b = (uint8_t)((bf + m) * 255.0f);
}

// ---------------------------------------------------------------------------
// ClusterBinding — maps MicroProto properties to Matter clusters
// ---------------------------------------------------------------------------
struct ClusterBinding {
    MicroProto::Property<bool>*              onOff      = nullptr;
    MicroProto::Property<uint8_t>*           brightness = nullptr;
    MicroProto::ArrayProperty<uint8_t, 3>*   color      = nullptr;

    const char* vendorName   = "MicroProto";
    const char* productName  = "LED Strip";
    const char* serialNumber = "MP-001";
    const char* hwVersionStr = "1.0";
    const char* swVersionStr = "1.0";

    bool     commissioned = false;  // Set by AddNOC handler
    uint32_t dataVersion  = 0;      // Incremented on state changes

    // --- Read attribute ---
    bool readAttribute(uint16_t endpoint, uint32_t cluster, uint32_t attr,
                       TLVWriter& wr, uint8_t dataTag) const {
        if (endpoint == 0) return readEndpoint0(cluster, attr, wr, dataTag);
        if (endpoint == 1) return readEndpoint1(cluster, attr, wr, dataTag);
        return false;
    }

    // --- Write attribute ---
    bool writeAttribute(uint16_t endpoint, uint32_t cluster, uint32_t attr,
                        TLVReader& rd) {
        if (endpoint != 1) return false;

        if (cluster == kClusterOnOff && attr == kAttrOnOff && onOff) {
            onOff->set(rd.getBool());
            dataVersion++;
            return true;
        }
        if (cluster == kClusterLevelControl && attr == kAttrCurrentLevel && brightness) {
            brightness->set(rd.getU8());
            dataVersion++;
            return true;
        }
        if (cluster == kClusterColorControl && color) {
            const auto& rgb = color->get();
            uint8_t oldHue, oldSat;
            rgbToHueSat(rgb[0], rgb[1], rgb[2], oldHue, oldSat);
            uint8_t level = brightness ? brightness->get() : 254;

            if (attr == kAttrCurrentHue) {
                uint8_t r, g, b;
                hueSatToRgb(rd.getU8(), oldSat, level, r, g, b);
                color->set({r, g, b});
                dataVersion++;
                return true;
            }
            if (attr == kAttrCurrentSaturation) {
                uint8_t r, g, b;
                hueSatToRgb(oldHue, rd.getU8(), level, r, g, b);
                color->set({r, g, b});
                dataVersion++;
                return true;
            }
        }
        return false;
    }

    // --- Handle command ---
    bool handleCommand(uint16_t endpoint, uint32_t cluster, uint32_t command,
                       TLVReader& rd, TLVWriter& wr) {
        if (endpoint != 1) return false;
        (void)wr;

        // OnOff commands
        if (cluster == kClusterOnOff && onOff) {
            if (command == kCmdOff)    { onOff->set(false);         dataVersion++; return true; }
            if (command == kCmdOn)     { onOff->set(true);          dataVersion++; return true; }
            if (command == kCmdToggle) { onOff->set(!onOff->get()); dataVersion++; return true; }
        }

        // LevelControl: MoveToLevel / MoveToLevelWithOnOff
        if (cluster == kClusterLevelControl && brightness) {
            if (command == kCmdMoveToLevel || command == kCmdMoveToLevelWithOnOff) {
                while (rd.next()) {
                    if (rd.tag() == 0) { brightness->set(rd.getU8()); break; }
                }
                if (command == kCmdMoveToLevelWithOnOff && onOff) {
                    onOff->set(brightness->get() > 0);
                }
                dataVersion++;
                return true;
            }
        }

        // ColorControl: MoveToHueAndSaturation
        if (cluster == kClusterColorControl && color) {
            if (command == kCmdMoveToHueSat) {
                uint8_t newHue = 0, newSat = 0;
                while (rd.next()) {
                    if (rd.isEnd()) break;
                    if (rd.tag() == 0) newHue = rd.getU8();
                    if (rd.tag() == 1) newSat = rd.getU8();
                }
                uint8_t level = brightness ? brightness->get() : 254;
                uint8_t r, g, b;
                hueSatToRgb(newHue, newSat, level, r, g, b);
                color->set({r, g, b});
                dataVersion++;
                return true;
            }
        }

        return false;
    }

private:
    // --- Endpoint 0: Root Node ---
    bool readEndpoint0(uint32_t cluster, uint32_t attr,
                       TLVWriter& wr, uint8_t tag) const {
        if (cluster == kClusterDescriptor) {
            if (attr == kAttrDeviceTypeList) {
                wr.openArray(tag);
                wr.openStruct(kAnon);
                wr.putU32(0, kDevTypeRootNode);
                wr.putU16(1, 1);
                wr.closeContainer();
                wr.closeContainer();
                return true;
            }
            if (attr == kAttrServerList) {
                wr.openArray(tag);
                wr.putU32(kAnon, kClusterDescriptor);
                wr.putU32(kAnon, kClusterAccessControl);
                wr.putU32(kAnon, kClusterBasicInfo);
                wr.putU32(kAnon, kClusterGeneralCommissioning);
                wr.putU32(kAnon, kClusterNetworkCommissioning);
                wr.putU32(kAnon, kClusterAdminCommissioning);
                wr.putU32(kAnon, kClusterGeneralDiagnostics);
                wr.putU32(kAnon, kClusterOperationalCredentials);
                wr.putU32(kAnon, kClusterGroupKeyMgmt);
                wr.closeContainer();
                return true;
            }
            if (attr == kAttrClientList || attr == kAttrPartsList) {
                wr.openArray(tag);
                if (attr == kAttrPartsList) wr.putU16(kAnon, 1);
                wr.closeContainer();
                return true;
            }
        }

        if (cluster == kClusterBasicInfo) {
            switch (attr) {
                case kAttrVendorName:    wr.putString(tag, vendorName); return true;
                case kAttrVendorID:      wr.putU16(tag, kTestVendorId); return true;
                case kAttrProductName:   wr.putString(tag, productName); return true;
                case kAttrProductID:     wr.putU16(tag, kTestProductId); return true;
                case kAttrNodeLabel:     wr.putString(tag, productName); return true;
                case kAttrHWVersion:     wr.putU16(tag, 1); return true;
                case kAttrHWVersionStr:  wr.putString(tag, hwVersionStr); return true;
                case kAttrSWVersion:     wr.putU32(tag, 1); return true;
                case kAttrSWVersionStr:  wr.putString(tag, swVersionStr); return true;
                case kAttrSerialNumber:  wr.putString(tag, serialNumber); return true;
                case kAttrUniqueID:      wr.putString(tag, serialNumber); return true;
                case kAttrCapabilityMinima:
                    wr.openStruct(tag);
                    wr.putU16(0, 3);
                    wr.putU16(1, 3);
                    wr.closeContainer();
                    return true;
                default: break;
            }
        }

        if (cluster == kClusterGeneralCommissioning) {
            if (attr == kAttrBreadcrumb) { wr.putU64(tag, 0); return true; }
            if (attr == kAttrBasicCommissioningInfo) {
                wr.openStruct(tag);
                wr.putU16(0, 60);
                wr.putU16(1, 900);
                wr.closeContainer();
                return true;
            }
            if (attr == kAttrRegulatoryConfig) { wr.putU8(tag, 0); return true; }
            if (attr == kAttrLocationCapability) { wr.putU8(tag, 2); return true; }
        }

        if (cluster == kClusterNetworkCommissioning) {
            if (attr == kAttrFeatureMap) { wr.putU32(tag, 0x01); return true; }
            if (attr == kAttrClusterRevision) { wr.putU16(tag, 1); return true; }
        }

        if (cluster == kClusterOperationalCredentials) {
            if (attr == kAttrSupportedFabrics)    { wr.putU8(tag, 1); return true; }
            if (attr == kAttrCommissionedFabrics) { wr.putU8(tag, commissioned ? 1 : 0); return true; }
            if (attr == kAttrCurrentFabricIndex)  { wr.putU8(tag, 1); return true; }
        }

        if (cluster == kClusterAdminCommissioning) {
            if (attr == 0x0000) { wr.putU8(tag, 0); return true; }   // WindowStatus: 0=not open
            if (attr == 0x0001) { wr.putNull(tag); return true; }    // AdminFabricIndex: null
            if (attr == 0x0002) { wr.putNull(tag); return true; }    // AdminVendorId: null
        }

        return readGlobalAttribute(cluster, attr, wr, tag);
    }

    // --- Endpoint 1: Extended Color Light ---
    bool readEndpoint1(uint32_t cluster, uint32_t attr,
                       TLVWriter& wr, uint8_t tag) const {
        if (cluster == kClusterDescriptor) {
            if (attr == kAttrDeviceTypeList) {
                wr.openArray(tag);
                wr.openStruct(kAnon);
                wr.putU32(0, kDevTypeExtendedColorLight);
                wr.putU16(1, 1);
                wr.closeContainer();
                wr.closeContainer();
                return true;
            }
            if (attr == kAttrServerList) {
                wr.openArray(tag);
                wr.putU32(kAnon, kClusterDescriptor);
                wr.putU32(kAnon, kClusterIdentify);
                wr.putU32(kAnon, kClusterGroups);
                wr.putU32(kAnon, kClusterOnOff);
                wr.putU32(kAnon, kClusterLevelControl);
                wr.putU32(kAnon, kClusterColorControl);
                wr.closeContainer();
                return true;
            }
            if (attr == kAttrClientList || attr == kAttrPartsList) {
                wr.openArray(tag);
                wr.closeContainer();
                return true;
            }
        }

        if (cluster == kClusterOnOff && attr == kAttrOnOff) {
            wr.putBool(tag, onOff ? onOff->get() : false);
            return true;
        }

        if (cluster == kClusterLevelControl) {
            if (attr == kAttrCurrentLevel) {
                wr.putU8(tag, brightness ? brightness->get() : 254);
                return true;
            }
            if (attr == kAttrMinLevel) { wr.putU8(tag, 1); return true; }
            if (attr == kAttrMaxLevel) { wr.putU8(tag, 254); return true; }
        }

        if (cluster == kClusterColorControl) {
            uint8_t hue = 0, sat = 0;
            if (color) {
                const auto& rgb = color->get();
                rgbToHueSat(rgb[0], rgb[1], rgb[2], hue, sat);
            }
            if (attr == kAttrCurrentHue)        { wr.putU8(tag, hue); return true; }
            if (attr == kAttrCurrentSaturation) { wr.putU8(tag, sat); return true; }
            if (attr == kAttrColorMode)         { wr.putU8(tag, 0); return true; }
            if (attr == kAttrEnhancedColorMode) { wr.putU8(tag, 0); return true; }
            if (attr == kAttrColorCapabilities) { wr.putU16(tag, 0x01); return true; }
        }

        return readGlobalAttribute(cluster, attr, wr, tag);
    }

    bool readGlobalAttribute(uint32_t cluster, uint32_t attr,
                              TLVWriter& wr, uint8_t tag) const {
        if (attr == kAttrClusterRevision) { wr.putU16(tag, 1); return true; }
        if (attr == kAttrFeatureMap) {
            uint32_t fm = 0;
            if (cluster == kClusterOnOff)        fm = 0x01; // LT (Lighting)
            if (cluster == kClusterLevelControl) fm = 0x03; // OO + LT
            if (cluster == kClusterColorControl) fm = 0x01; // HS (Hue/Saturation)
            wr.putU32(tag, fm);
            return true;
        }
        if (attr == kAttrAttributeList) {
            wr.openArray(tag);
            // Cluster-specific attributes
            if (cluster == kClusterOnOff) {
                wr.putU32(kAnon, kAttrOnOff);
            } else if (cluster == kClusterLevelControl) {
                wr.putU32(kAnon, kAttrCurrentLevel);
                wr.putU32(kAnon, kAttrMinLevel);
                wr.putU32(kAnon, kAttrMaxLevel);
            } else if (cluster == kClusterColorControl) {
                wr.putU32(kAnon, kAttrCurrentHue);
                wr.putU32(kAnon, kAttrCurrentSaturation);
                wr.putU32(kAnon, kAttrColorMode);
                wr.putU32(kAnon, kAttrEnhancedColorMode);
                wr.putU32(kAnon, kAttrColorCapabilities);
            } else if (cluster == kClusterDescriptor) {
                wr.putU32(kAnon, kAttrDeviceTypeList);
                wr.putU32(kAnon, kAttrServerList);
                wr.putU32(kAnon, kAttrClientList);
                wr.putU32(kAnon, kAttrPartsList);
            } else if (cluster == kClusterBasicInfo) {
                wr.putU32(kAnon, kAttrVendorName);
                wr.putU32(kAnon, kAttrVendorID);
                wr.putU32(kAnon, kAttrProductName);
                wr.putU32(kAnon, kAttrProductID);
                wr.putU32(kAnon, kAttrNodeLabel);
                wr.putU32(kAnon, kAttrHWVersion);
                wr.putU32(kAnon, kAttrHWVersionStr);
                wr.putU32(kAnon, kAttrSWVersion);
                wr.putU32(kAnon, kAttrSWVersionStr);
                wr.putU32(kAnon, kAttrSerialNumber);
                wr.putU32(kAnon, kAttrUniqueID);
                wr.putU32(kAnon, kAttrCapabilityMinima);
            } else if (cluster == kClusterAdminCommissioning) {
                wr.putU32(kAnon, 0x0000); // WindowStatus
                wr.putU32(kAnon, 0x0001); // AdminFabricIndex
                wr.putU32(kAnon, 0x0002); // AdminVendorId
            }
            // Global attributes always present
            wr.putU32(kAnon, kAttrClusterRevision);
            wr.putU32(kAnon, kAttrFeatureMap);
            wr.putU32(kAnon, kAttrAttributeList);
            wr.putU32(kAnon, kAttrAcceptedCommandList);
            wr.putU32(kAnon, kAttrGeneratedCommandList);
            wr.closeContainer();
            return true;
        }
        if (attr == kAttrAcceptedCommandList) {
            wr.openArray(tag);
            if (cluster == kClusterOnOff) {
                wr.putU32(kAnon, kCmdOff);
                wr.putU32(kAnon, kCmdOn);
                wr.putU32(kAnon, kCmdToggle);
            } else if (cluster == kClusterLevelControl) {
                wr.putU32(kAnon, kCmdMoveToLevel);
                wr.putU32(kAnon, kCmdMoveToLevelWithOnOff);
            } else if (cluster == kClusterColorControl) {
                wr.putU32(kAnon, kCmdMoveToHueSat);
            } else if (cluster == kClusterGeneralCommissioning) {
                wr.putU32(kAnon, kCmdArmFailSafe);
                wr.putU32(kAnon, kCmdSetRegulatoryConfig);
                wr.putU32(kAnon, kCmdCommissioningComplete);
            } else if (cluster == kClusterOperationalCredentials) {
                wr.putU32(kAnon, kCmdAttestationRequest);
                wr.putU32(kAnon, kCmdCertChainRequest);
                wr.putU32(kAnon, kCmdCSRRequest);
                wr.putU32(kAnon, kCmdAddNOC);
                wr.putU32(kAnon, kCmdAddTrustedRootCert);
            }
            wr.closeContainer();
            return true;
        }
        if (attr == kAttrGeneratedCommandList) {
            wr.openArray(tag);
            if (cluster == kClusterGeneralCommissioning) {
                wr.putU32(kAnon, kCmdArmFailSafeResp);
                wr.putU32(kAnon, kCmdSetRegulatoryConfigResp);
                wr.putU32(kAnon, kCmdCommissioningCompleteResp);
            } else if (cluster == kClusterOperationalCredentials) {
                wr.putU32(kAnon, kCmdAttestationResponse);
                wr.putU32(kAnon, kCmdCertChainResponse);
                wr.putU32(kAnon, kCmdCSRResponse);
                wr.putU32(kAnon, kCmdNOCResponse);
            }
            wr.closeContainer();
            return true;
        }
        return false;
    }
};

} // namespace matter
