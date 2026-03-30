package device

import (
	"database/sql"
	"encoding/json"
	"log"
	"net/http"
	"strings"
	"sync"
	"time"

	"github.com/gorilla/websocket"
	"github.com/marso/ledstrip/gateway/internal/proto"
)

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool { return true },
}

type Device struct {
	ID       string    `json:"id"`
	Name     string    `json:"name"`
	IP       string    `json:"ip"`
	Online   bool      `json:"online"`
	LastSeen time.Time `json:"lastSeen"`
	Token    string    `json:"-"`
	conn     *websocket.Conn
	mu       sync.Mutex

	active bool // true = device is sending full MicroProto
}

func (d *Device) Send(data []byte) error {
	d.mu.Lock()
	defer d.mu.Unlock()
	if d.conn == nil {
		return nil
	}
	return d.conn.WriteMessage(websocket.BinaryMessage, data)
}

type Registry struct {
	mu      sync.RWMutex
	devices map[string]*Device
	db      *sql.DB
}

func NewRegistry(db *sql.DB) *Registry {
	db.Exec(`CREATE TABLE IF NOT EXISTS devices (
		id TEXT PRIMARY KEY,
		name TEXT,
		last_seen DATETIME
	)`)

	r := &Registry{
		devices: make(map[string]*Device),
		db:      db,
	}

	rows, err := db.Query("SELECT id, name, last_seen FROM devices")
	if err == nil {
		defer rows.Close()
		for rows.Next() {
			var d Device
			var lastSeen string
			rows.Scan(&d.ID, &d.Name, &lastSeen)
			d.Online = false
			d.LastSeen, _ = time.Parse("2006-01-02 15:04:05", lastSeen)
			r.devices[d.ID] = &d
		}
	}

	return r
}

func (r *Registry) Get(id string) *Device {
	r.mu.RLock()
	defer r.mu.RUnlock()
	return r.devices[id]
}

func (r *Registry) List() []*Device {
	r.mu.RLock()
	defer r.mu.RUnlock()
	list := make([]*Device, 0, len(r.devices))
	for _, d := range r.devices {
		list = append(list, d)
	}
	return list
}

// HandleDeviceWS handles incoming WebSocket connections from ESP32 devices
func (r *Registry) HandleDeviceWS(w http.ResponseWriter, req *http.Request) {
	token := req.URL.Query().Get("token")
	deviceID := req.URL.Query().Get("id")
	deviceName := req.URL.Query().Get("name")
	deviceIP := req.URL.Query().Get("ip")

	if token == "" || deviceID == "" {
		http.Error(w, "missing token or id", http.StatusUnauthorized)
		return
	}

	conn, err := upgrader.Upgrade(w, req, nil)
	if err != nil {
		log.Printf("Device WS upgrade failed: %v", err)
		return
	}

	dev := &Device{
		ID:       deviceID,
		Name:     deviceName,
		IP:       deviceIP,
		Online:   true,
		LastSeen: time.Now(),
		Token:    token,
		conn:     conn,
	}

	r.mu.Lock()
	r.devices[deviceID] = dev
	r.mu.Unlock()

	r.db.Exec("INSERT OR REPLACE INTO devices (id, name, last_seen) VALUES (?, ?, ?)",
		deviceID, deviceName, time.Now().Format("2006-01-02 15:04:05"))

	log.Printf("Device connected: %s (%s) from %s", deviceName, deviceID, deviceIP)

	// No read deadline — device may be idle for long periods
	conn.SetReadLimit(0) // no message size limit
	conn.SetPingHandler(func(appData string) error {
		return conn.WriteControl(websocket.PongMessage, []byte(appData), time.Now().Add(5*time.Second))
	})

	defer func() {
		r.mu.Lock()
		if d, ok := r.devices[deviceID]; ok {
			d.Online = false
			d.conn = nil
			d.active = false
		}
		r.mu.Unlock()
		conn.Close()
		log.Printf("Device disconnected: %s (%s)", deviceName, deviceID)
	}()

	for {
		msgType, data, err := conn.ReadMessage()
		if err != nil {
			log.Printf("Device %s read error: %v", deviceID, err)
			break
		}
		if msgType != websocket.BinaryMessage || len(data) == 0 {
			continue
		}

		dev.mu.Lock()
		dev.LastSeen = time.Now()
		dev.mu.Unlock()

		if proto.IsHello(data) {
			isResponse, idle := proto.ParseHelloFlags(data[0])
			if idle && !isResponse {
				// Device registering idle
				log.Printf("Device %s registered idle", deviceID)
				dev.Send(proto.EncodeHello(true))
				continue
			}
			if isResponse {
				dev.mu.Lock()
				dev.active = !idle
				dev.mu.Unlock()
			}
		}

		// Forward everything to connected web clients
		r.forwardToClients(deviceID, data)
	}
}

// DeactivateDevice sends a deactivate (idle) HELLO to a device
func (r *Registry) DeactivateDevice(deviceID string) {
	dev := r.Get(deviceID)
	if dev == nil || !dev.Online {
		return
	}

	log.Printf("Deactivating device %s", deviceID)
	dev.Send(proto.EncodeHello(true)) // idle=true → deactivate

	dev.mu.Lock()
	dev.active = false
	dev.mu.Unlock()
}

// Client management

var (
	clientsMu    sync.RWMutex
	clients      = make(map[string][]*websocket.Conn) // deviceID → client connections
	clientCounts = make(map[string]int)                // deviceID → count
)

func RegisterClient(deviceID string, conn *websocket.Conn, registry *Registry) {
	clientsMu.Lock()
	clients[deviceID] = append(clients[deviceID], conn)
	clientCounts[deviceID]++
	clientsMu.Unlock()
}

func UnregisterClient(deviceID string, conn *websocket.Conn, registry *Registry) {
	clientsMu.Lock()
	conns := clients[deviceID]
	for i, c := range conns {
		if c == conn {
			clients[deviceID] = append(conns[:i], conns[i+1:]...)
			break
		}
	}
	clientCounts[deviceID]--
	count := clientCounts[deviceID]
	if count <= 0 {
		clientCounts[deviceID] = 0
	}
	clientsMu.Unlock()

	if count <= 0 {
		// Last client — deactivate device
		registry.DeactivateDevice(deviceID)
	}
}

func (r *Registry) forwardToClients(deviceID string, data []byte) {
	clientsMu.RLock()
	conns := clients[deviceID]
	clientsMu.RUnlock()

	for _, c := range conns {
		c.WriteMessage(websocket.BinaryMessage, data)
	}
}

// HTTP API

func (r *Registry) HandleListDevices(w http.ResponseWriter, req *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(r.List())
}

func (r *Registry) HandleDeviceAPI(w http.ResponseWriter, req *http.Request) {
	parts := strings.Split(strings.TrimPrefix(req.URL.Path, "/api/devices/"), "/")
	if len(parts) == 0 || parts[0] == "" {
		http.NotFound(w, req)
		return
	}
	deviceID := parts[0]

	if req.Method == "DELETE" {
		r.mu.Lock()
		if d, ok := r.devices[deviceID]; ok && d.conn != nil {
			d.conn.Close()
		}
		delete(r.devices, deviceID)
		r.mu.Unlock()
		r.db.Exec("DELETE FROM devices WHERE id = ?", deviceID)
		w.WriteHeader(http.StatusNoContent)
		return
	}

	dev := r.Get(deviceID)
	if dev == nil {
		http.NotFound(w, req)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(dev)
}
