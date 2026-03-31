package auth

import (
	"crypto/rand"
	"database/sql"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"strings"
	"time"

	_ "github.com/mattn/go-sqlite3"
	"golang.org/x/crypto/bcrypt"
)

type Store struct {
	db *sql.DB
}

func NewStore(dbPath string) (*Store, error) {
	db, err := sql.Open("sqlite3", dbPath)
	if err != nil {
		return nil, err
	}

	// Create tables
	_, err = db.Exec(`
		CREATE TABLE IF NOT EXISTS users (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			username TEXT UNIQUE NOT NULL,
			password_hash TEXT NOT NULL,
			created_at DATETIME DEFAULT CURRENT_TIMESTAMP
		);
		CREATE TABLE IF NOT EXISTS device_tokens (
			token TEXT PRIMARY KEY,
			user_id INTEGER NOT NULL,
			name TEXT,
			created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
			FOREIGN KEY (user_id) REFERENCES users(id)
		);
		CREATE TABLE IF NOT EXISTS sessions (
			token TEXT PRIMARY KEY,
			user_id INTEGER NOT NULL,
			expires_at DATETIME NOT NULL,
			FOREIGN KEY (user_id) REFERENCES users(id)
		);
	`)
	if err != nil {
		return nil, err
	}

	return &Store{db: db}, nil
}

func (s *Store) Close() error {
	return s.db.Close()
}

func (s *Store) DB() *sql.DB {
	return s.db
}

// EnsureDefaultUser creates admin/admin if no users exist
func (s *Store) EnsureDefaultUser() error {
	var count int
	s.db.QueryRow("SELECT COUNT(*) FROM users").Scan(&count)
	if count > 0 {
		return nil
	}
	hash, err := bcrypt.GenerateFromPassword([]byte("admin"), bcrypt.DefaultCost)
	if err != nil {
		return err
	}
	_, err = s.db.Exec("INSERT INTO users (username, password_hash) VALUES (?, ?)", "admin", string(hash))
	if err != nil {
		return err
	}
	log.Println("Created default user: admin/admin")
	return nil
}

func generateToken() string {
	b := make([]byte, 16)
	rand.Read(b)
	return hex.EncodeToString(b)
}

// HandleLogin authenticates user, returns session token
func (s *Store) HandleLogin(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		http.Error(w, "POST only", http.StatusMethodNotAllowed)
		return
	}

	var req struct {
		Username string `json:"username"`
		Password string `json:"password"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad request", http.StatusBadRequest)
		return
	}

	var userID int
	var hash string
	err := s.db.QueryRow("SELECT id, password_hash FROM users WHERE username = ?", req.Username).Scan(&userID, &hash)
	if err != nil || bcrypt.CompareHashAndPassword([]byte(hash), []byte(req.Password)) != nil {
		http.Error(w, "invalid credentials", http.StatusUnauthorized)
		return
	}

	token := generateToken()
	expires := time.Now().Add(30 * 24 * time.Hour) // 30 days
	s.db.Exec("INSERT INTO sessions (token, user_id, expires_at) VALUES (?, ?, ?)", token, userID, expires)

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{"token": token})
}

// HandleCreateToken creates a device registration token
func (s *Store) HandleCreateToken(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		http.Error(w, "POST only", http.StatusMethodNotAllowed)
		return
	}

	userID := r.Header.Get("X-User-ID")

	var req struct {
		Name string `json:"name"`
	}
	json.NewDecoder(r.Body).Decode(&req)

	token := generateToken()
	s.db.Exec("INSERT INTO device_tokens (token, user_id, name) VALUES (?, ?, ?)",
		token, userID, req.Name)

	http.SetCookie(w, &http.Cookie{
		Name:     "session",
		Value:    token,
		Path:     "/",
		MaxAge:   30 * 24 * 3600,
		HttpOnly: true,
		Secure:   true,
		SameSite: http.SameSiteLaxMode,
	})

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{"token": token})
}

// HandleListTokens returns all device tokens for the authenticated user
func (s *Store) HandleListTokens(w http.ResponseWriter, r *http.Request) {
	userID := r.Header.Get("X-User-ID")
	rows, err := s.db.Query("SELECT token, name, created_at FROM device_tokens WHERE user_id = ?", userID)
	if err != nil {
		http.Error(w, "db error", http.StatusInternalServerError)
		return
	}
	defer rows.Close()

	type TokenInfo struct {
		Token     string `json:"token"`
		Name      string `json:"name"`
		CreatedAt string `json:"createdAt"`
	}
	var tokens []TokenInfo
	for rows.Next() {
		var t TokenInfo
		rows.Scan(&t.Token, &t.Name, &t.CreatedAt)
		tokens = append(tokens, t)
	}
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(tokens)
}

// HandleDeleteToken deletes a device token
func (s *Store) HandleDeleteToken(w http.ResponseWriter, r *http.Request) {
	if r.Method != "DELETE" {
		http.Error(w, "DELETE only", http.StatusMethodNotAllowed)
		return
	}
	token := strings.TrimPrefix(r.URL.Path, "/api/auth/token/")
	if token == "" {
		http.Error(w, "missing token", http.StatusBadRequest)
		return
	}
	s.db.Exec("DELETE FROM device_tokens WHERE token = ?", token)
	w.WriteHeader(http.StatusNoContent)
}

// ValidateDeviceToken checks if a device token is valid, returns user_id
func (s *Store) ValidateDeviceToken(token string) (int, bool) {
	var userID int
	err := s.db.QueryRow("SELECT user_id FROM device_tokens WHERE token = ?", token).Scan(&userID)
	return userID, err == nil
}

// RequireAuth middleware — checks Authorization header or cookie
func (s *Store) RequireAuth(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		token := ""

		// Check Authorization header
		auth := r.Header.Get("Authorization")
		if strings.HasPrefix(auth, "Bearer ") {
			token = strings.TrimPrefix(auth, "Bearer ")
		}

		// Check cookie
		if token == "" {
			if cookie, err := r.Cookie("session"); err == nil {
				token = cookie.Value
			}
		}

		// Check query param
		if token == "" {
			token = r.URL.Query().Get("session")
		}

		if token == "" {
			log.Printf("[Auth] No token found in request to %s", r.URL.Path)
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}

		var userID int
		var expiresStr string
		err := s.db.QueryRow("SELECT user_id, expires_at FROM sessions WHERE token = ?", token).Scan(&userID, &expiresStr)
		if err != nil {
			log.Printf("[Auth] Token lookup failed: %v", err)
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}
		expires, _ := time.Parse("2006-01-02 15:04:05-07:00", expiresStr)
		if expires.IsZero() {
			expires, _ = time.Parse("2006-01-02T15:04:05Z", expiresStr)
		}
		if !expires.IsZero() && time.Now().After(expires) {
			log.Printf("[Auth] Token expired")
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}

		r.Header.Set("X-User-ID", fmt.Sprintf("%d", userID))
		next(w, r)
	}
}
