#pragma once

#include <cstdint>

// BLE command types (app → device)
static constexpr uint8_t BLE_CMD_SET_CLOCK    = 0x01;  // [unix_time:4]
static constexpr uint8_t BLE_CMD_SET_LOCATION = 0x02;  // [nodeA:1][nodeB:1]
static constexpr uint8_t BLE_CMD_SEND_TEXT    = 0x03;  // [len:1][text:N]
static constexpr uint8_t BLE_CMD_GET_STATE    = 0x04;  // (empty)
static constexpr uint8_t BLE_CMD_GET_MESSAGES = 0x05;  // [senderId:1][fromSeq:2]
static constexpr uint8_t BLE_CMD_GET_SELF_INFO = 0x06; // (empty)

// BLE response/notification types (device → app)
static constexpr uint8_t BLE_RESP_STATE       = 0x80;  // [count:1][entries:N]
static constexpr uint8_t BLE_RESP_MESSAGE     = 0x81;  // [senderId:1][seq:2][ts:4][type:1][payload:N]
static constexpr uint8_t BLE_NOTIFY_INCOMING  = 0x82;  // same as MessageResp
static constexpr uint8_t BLE_NOTIFY_PRESENCE  = 0x83;  // [senderId:1][presence:1]
static constexpr uint8_t BLE_RESP_SELF_INFO  = 0x84;  // [deviceId:1][clock:4][activeSenders:1]
