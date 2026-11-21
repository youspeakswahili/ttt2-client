// ttt2d: matchmaking and relay server for the TTT2 Client. See PROTOCOL.md.
package main

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"flag"
	"fmt"
	"log"
	"math/big"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"time"
)

const version = "0.1.0"

// loadOrMakeCert keeps one self-signed certificate in the data folder. Clients pin its fingerprint,
// so it must survive restarts and updates (back up cert.pem and key.pem with players.json).
func loadOrMakeCert(dir string) (tls.Certificate, error) {
	certPath, keyPath := filepath.Join(dir, "cert.pem"), filepath.Join(dir, "key.pem")
	if _, err := os.Stat(certPath); err == nil {
		return tls.LoadX509KeyPair(certPath, keyPath)
	}
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		return tls.Certificate{}, err
	}
	serial, _ := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 62))
	tmpl := &x509.Certificate{
		SerialNumber:          serial,
		Subject:               pkix.Name{CommonName: "ttt2d"},
		NotBefore:             time.Now().Add(-time.Hour),
		NotAfter:              time.Now().AddDate(30, 0, 0),
		KeyUsage:              x509.KeyUsageDigitalSignature,
		ExtKeyUsage:           []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		BasicConstraintsValid: true,
		DNSNames:              []string{"ttt2d"},
	}
	der, err := x509.CreateCertificate(rand.Reader, tmpl, tmpl, &key.PublicKey, key)
	if err != nil {
		return tls.Certificate{}, err
	}
	keyDer, err := x509.MarshalECPrivateKey(key)
	if err != nil {
		return tls.Certificate{}, err
	}
	if err := os.WriteFile(keyPath, pem.EncodeToMemory(&pem.Block{Type: "EC PRIVATE KEY", Bytes: keyDer}), 0o600); err != nil {
		return tls.Certificate{}, err
	}
	if err := os.WriteFile(certPath, pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}), 0o644); err != nil {
		return tls.Certificate{}, err
	}
	return tls.LoadX509KeyPair(certPath, keyPath)
}

func fingerprint(c tls.Certificate) string {
	sum := sha256.Sum256(c.Certificate[0])
	parts := make([]string, len(sum))
	for i, b := range sum {
		parts[i] = fmt.Sprintf("%02X", b)
	}
	return strings.Join(parts, ":")
}

func main() {
	listen := flag.String("listen", ":443", "TCP address for the clients (secure WebSocket)")
	udpAddr := flag.String("udp", ":27620", "UDP address for hole punching and the relay")
	data := flag.String("data", "/var/lib/ttt2d", "folder for players, matches and the certificate")
	printFP := flag.Bool("fingerprint", false, "print the certificate fingerprint (for the client) and exit")
	showVersion := flag.Bool("version", false, "print the version and exit")
	flag.Parse()
	if *showVersion {
		fmt.Println("ttt2d", version)
		return
	}
	if err := os.MkdirAll(*data, 0o700); err != nil {
		log.Fatal(err)
	}
	cert, err := loadOrMakeCert(*data)
	if err != nil {
		log.Fatalf("certificate: %v", err)
	}
	if *printFP {
		fmt.Println(fingerprint(cert))
		return
	}
	store, err := OpenStore(*data)
	if err != nil {
		log.Fatalf("data: %v", err)
	}
	udp, err := NewUDPServer(*udpAddr)
	if err != nil {
		log.Fatalf("udp: %v", err)
	}
	lobby := NewLobby(store, udp)
	go udp.Serve()
	go func() {
		for now := range time.Tick(500 * time.Millisecond) {
			lobby.Tick(now)
		}
	}()
	go func() {
		for range time.Tick(5 * time.Second) {
			lobby.BroadcastStats()
		}
	}()
	go func() {
		for range time.Tick(time.Second) {
			if err := store.Flush(); err != nil {
				log.Printf("saving players: %v", err)
			}
		}
	}()

	mux := http.NewServeMux()
	mux.HandleFunc("/v1", func(w http.ResponseWriter, r *http.Request) {
		c, err := wsUpgrade(w, r)
		if err != nil {
			return
		}
		lobby.Serve(c, r.RemoteAddr)
	})
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/plain")
		fmt.Fprintln(w, "ttt2d", version)
	})
	srv := &http.Server{
		Addr:              *listen,
		Handler:           mux,
		TLSConfig:         &tls.Config{Certificates: []tls.Certificate{cert}, MinVersion: tls.VersionTLS12},
		ReadHeaderTimeout: 10 * time.Second,
	}
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGINT, syscall.SIGTERM)
	go func() {
		<-stop
		store.Flush()
		log.Printf("stopped")
		os.Exit(0)
	}()
	log.Printf("ttt2d %s: clients on %s (TLS), UDP on %s, data in %s", version, *listen, *udpAddr, *data)
	log.Printf("certificate fingerprint: %s", fingerprint(cert))
	log.Fatal(srv.ListenAndServeTLS("", ""))
}
