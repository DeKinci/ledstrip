package proxy

import (
	"log"
	"net/http"
	"strings"
	"time"

	"github.com/gorilla/websocket"
	"github.com/marso/ledstrip/gateway/internal/device"
)

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool { return true },
}

type Manager struct {
	registry *device.Registry
}

func NewManager(registry *device.Registry) *Manager {
	return &Manager{registry: registry}
}

// HandleClientWS proxies a web client's MicroProto WebSocket to a device.
// Gateway is a transparent proxy — all messages forwarded bidirectionally.
// Activation/deactivation is handled by client count.
func (m *Manager) HandleClientWS(w http.ResponseWriter, r *http.Request) {
	deviceID := strings.TrimPrefix(r.URL.Path, "/ws/proxy/")
	if deviceID == "" {
		http.Error(w, "missing device ID", http.StatusBadRequest)
		return
	}

	dev := m.registry.Get(deviceID)
	if dev == nil || !dev.Online {
		http.Error(w, "device not found or offline", http.StatusNotFound)
		return
	}

	clientConn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("Client WS upgrade failed: %v", err)
		return
	}
	defer clientConn.Close()

	clientConn.SetPingHandler(func(appData string) error {
		return clientConn.WriteControl(websocket.PongMessage, []byte(appData), time.Now().Add(5*time.Second))
	})

	device.RegisterClient(deviceID, clientConn, m.registry)
	defer device.UnregisterClient(deviceID, clientConn, m.registry)

	log.Printf("Client proxying to device %s", deviceID)

	// Forward client → device
	for {
		msgType, data, err := clientConn.ReadMessage()
		if err != nil {
			break
		}
		if msgType == websocket.BinaryMessage {
			if err := dev.Send(data); err != nil {
				break
			}
		}
	}

	log.Printf("Client disconnected from device %s", deviceID)
}
