package lightning

import "testing"

func TestParseRequest(t *testing.T) {
	data := []byte("GET /api/hello?name=world HTTP/1.1\r\nHost: test.com\r\nAccept: */*\r\n\r\n")
	req, err := ParseRequest(data)
	if err != nil {
		t.Fatal(err)
	}
	if req.Method != "GET" {
		t.Errorf("method = %q", req.Method)
	}
	if req.Path != "/api/hello?name=world" {
		t.Errorf("path = %q", req.Path)
	}
	if req.MinorVersion != 1 {
		t.Errorf("minor = %d", req.MinorVersion)
	}
	if len(req.Headers) != 2 {
		t.Fatalf("headers = %d", len(req.Headers))
	}
	host, ok := req.Header("Host")
	if !ok || host != "test.com" {
		t.Errorf("Host = %q, ok=%v", host, ok)
	}
}

func TestParseResponse(t *testing.T) {
	data := []byte("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n\r\nhello")
	resp, err := ParseResponse(data)
	if err != nil {
		t.Fatal(err)
	}
	if resp.StatusCode != 200 {
		t.Errorf("status = %d", resp.StatusCode)
	}
	if resp.Reason != "OK" {
		t.Errorf("reason = %q", resp.Reason)
	}
	if resp.BodyOffset != len(data)-5 {
		t.Errorf("body offset = %d", resp.BodyOffset)
	}
}

func TestSimdBackend(t *testing.T) {
	b := SimdBackend()
	if b == "" {
		t.Error("empty simd backend")
	}
	t.Logf("SIMD backend: %s", b)
}
