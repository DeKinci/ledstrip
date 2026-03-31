package main

//go:generate npx tsx ../../../../infra/build/src/bundle-web.ts

import (
	"flag"
	"log"
	"net/http"

	"github.com/marso/ledstrip/gateway/internal/auth"
	"github.com/marso/ledstrip/gateway/internal/device"
	"github.com/marso/ledstrip/gateway/internal/proxy"
	"github.com/marso/ledstrip/gateway/internal/web"
)

func main() {
	addr := flag.String("addr", ":8080", "HTTP listen address")
	dbPath := flag.String("db", "gateway.db", "SQLite database path")
	flag.Parse()

	// Initialize auth (users, device tokens, sessions)
	authStore, err := auth.NewStore(*dbPath)
	if err != nil {
		log.Fatalf("Failed to init auth: %v", err)
	}
	defer authStore.Close()

	// Create default admin user if none exists
	if err := authStore.EnsureDefaultUser(); err != nil {
		log.Fatalf("Failed to create default user: %v", err)
	}

	// Device registry (persisted + in-memory)
	registry := device.NewRegistry(authStore.DB())

	// WS proxy (routes web clients to devices)
	proxyMgr := proxy.NewManager(registry)

	mux := http.NewServeMux()

	// Device WebSocket endpoint (ESP32 connects here)
	mux.HandleFunc("/ws/device", registry.HandleDeviceWS)

	// Client WebSocket endpoint (web UI proxies MicroProto to device)
	mux.HandleFunc("/ws/proxy/", proxyMgr.HandleClientWS)

	// Auth API
	mux.HandleFunc("/api/auth/login", authStore.HandleLogin)
	mux.HandleFunc("/api/auth/token", authStore.RequireAuth(authStore.HandleCreateToken))
	mux.HandleFunc("/api/auth/token/", authStore.RequireAuth(authStore.HandleDeleteToken))
	mux.HandleFunc("/api/auth/tokens", authStore.RequireAuth(authStore.HandleListTokens))

	// Device API
	mux.HandleFunc("/api/devices", authStore.RequireAuth(registry.HandleListDevices))
	mux.HandleFunc("/api/devices/", authStore.RequireAuth(registry.HandleDeviceAPI))

	// Gateway web UI (embedded static files + widget library)
	mux.Handle("/", web.Handler())

	log.Printf("MicroProto Gateway listening on %s", *addr)
	log.Fatal(http.ListenAndServe(*addr, mux))
}
