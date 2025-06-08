IN
handling of `BLE_GAP_EVENT_NOTIFY_RX`

inside
```
for (const auto& chr : svc->m_vChars) {
    if (chr->getHandle() == event->notify_rx.attr_handle) {
```

PLACE:
```
                        NIMBLE_LOGD(LOG_TAG, "Got Notification for characteristic %s (handle=0x%04X)", chr->getUUID().toString().c_str(), chr->getHandle());

                        struct os_mbuf* mbuf = event->notify_rx.om;

                        if (!mbuf) {
                            NIMBLE_LOGE(LOG_TAG, "Received null os_mbuf for handle 0x%04X", event->notify_rx.attr_handle);
                            break;
                        }

                        uint32_t data_len = OS_MBUF_PKTLEN(mbuf);
                        uint8_t* data_ptr = mbuf->om_data;

                        NIMBLE_LOGD(LOG_TAG, "os_mbuf=%p, data_ptr=%p, data_len=%u", mbuf, data_ptr, data_len);

                        if (!data_ptr || data_len == 0) {
                            NIMBLE_LOGW(LOG_TAG, "Notification data invalid: ptr=%p, len=%u", data_ptr, data_len);
                            break;
                        }

                        std::string value;
                        value.resize(data_len);
                        int rc = os_mbuf_copydata(mbuf, 0, data_len, (uint8_t*)value.data());
                        if (rc != 0) {
                            NIMBLE_LOGE(LOG_TAG, "os_mbuf_copydata failed: rc=%d", rc);
                            break;
                        }

                        chr->m_value.setValue(value);

                        bool hasCb = chr->m_notifyCallback != nullptr;
                        NIMBLE_LOGD(LOG_TAG, "Notify callback present: %s", hasCb ? "yes" : "no");

                        if (hasCb) {
                            chr->m_notifyCallback(chr, (uint8_t*)value.data(), data_len, !event->notify_rx.indication);
                        }

                        break;
```
