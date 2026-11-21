// release: signs and zips a client release.
//
//	release -genkey -key release-signing.key     makes a key pair; prints the public key for client/update_key.h
//	release -key release-signing.key -dir "pkg/TTT2 Client" -version 0.1.7 -out TTT2-Client-0.1.7.zip
//
// The zip holds the folder "TTT2 Client/" with every file, plus manifest.txt (version and the SHA-512 of each file)
// and manifest.sig (the Ed25519 signature of manifest.txt, hex). The client installs an update only when the
// signature checks out against the public key built into it and every file matches its hash.
package main

import (
	"archive/zip"
	"crypto/ed25519"
	"crypto/rand"
	"crypto/sha512"
	"encoding/hex"
	"flag"
	"fmt"
	"io"
	"io/fs"
	"log"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

func main() {
	genkey := flag.Bool("genkey", false, "make a new signing key")
	keyPath := flag.String("key", "release-signing.key", "the private signing key (keep it secret)")
	dir := flag.String("dir", "", "folder to release (becomes \"TTT2 Client/\" in the zip)")
	version := flag.String("version", "", "version, e.g. 0.1.7")
	out := flag.String("out", "", "zip to write")
	flag.Parse()

	if *genkey {
		if _, err := os.Stat(*keyPath); err == nil {
			log.Fatalf("%s already exists; not overwriting a signing key", *keyPath)
		}
		pub, priv, err := ed25519.GenerateKey(rand.Reader)
		if err != nil {
			log.Fatal(err)
		}
		if err := os.WriteFile(*keyPath, []byte(hex.EncodeToString(priv.Seed())+"\n"), 0o600); err != nil {
			log.Fatal(err)
		}
		fmt.Println(cArray(pub))
		return
	}
	if *dir == "" || *version == "" || *out == "" {
		log.Fatal("need -dir, -version and -out")
	}
	seedHex, err := os.ReadFile(*keyPath)
	if err != nil {
		log.Fatal(err)
	}
	seed, err := hex.DecodeString(strings.TrimSpace(string(seedHex)))
	if err != nil || len(seed) != ed25519.SeedSize {
		log.Fatal("the key file is not a signing key")
	}
	priv := ed25519.NewKeyFromSeed(seed)

	var files []string
	err = filepath.WalkDir(*dir, func(p string, d fs.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return err
		}
		rel, _ := filepath.Rel(*dir, p)
		rel = filepath.ToSlash(rel)
		if rel == "manifest.txt" || rel == "manifest.sig" {
			return nil
		}
		files = append(files, rel)
		return nil
	})
	if err != nil {
		log.Fatal(err)
	}
	sort.Strings(files)

	var manifest strings.Builder
	fmt.Fprintf(&manifest, "ttt2-client %s\n", *version)
	for _, f := range files {
		data, err := os.ReadFile(filepath.Join(*dir, f))
		if err != nil {
			log.Fatal(err)
		}
		sum := sha512.Sum512(data)
		fmt.Fprintf(&manifest, "%s %d %s\n", hex.EncodeToString(sum[:]), len(data), f)
	}
	sig := ed25519.Sign(priv, []byte(manifest.String()))

	zf, err := os.Create(*out)
	if err != nil {
		log.Fatal(err)
	}
	zw := zip.NewWriter(zf)
	add := func(name string, r io.Reader) {
		w, err := zw.CreateHeader(&zip.FileHeader{Name: "TTT2 Client/" + name, Method: zip.Deflate})
		if err != nil {
			log.Fatal(err)
		}
		if _, err := io.Copy(w, r); err != nil {
			log.Fatal(err)
		}
	}
	for _, f := range files {
		fh, err := os.Open(filepath.Join(*dir, f))
		if err != nil {
			log.Fatal(err)
		}
		add(f, fh)
		fh.Close()
	}
	add("manifest.txt", strings.NewReader(manifest.String()))
	add("manifest.sig", strings.NewReader(hex.EncodeToString(sig)+"\n"))
	if err := zw.Close(); err != nil {
		log.Fatal(err)
	}
	zf.Close()
	fmt.Printf("%s: %d files, signed\n", *out, len(files))
}

func cArray(b []byte) string {
	parts := make([]string, len(b))
	for i, x := range b {
		parts[i] = fmt.Sprintf("0x%02x", x)
	}
	return "{ " + strings.Join(parts, ", ") + " }"
}
