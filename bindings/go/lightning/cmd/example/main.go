// Example: parse HTTP with lightning in Go.
package main

import (
	"fmt"

	lightning "lightning"
)

func main() {
	fmt.Printf("SIMD backend: %s\n\n", lightning.SimdBackend())

	reqData := []byte("GET /api/hello?name=world HTTP/1.1\r\nHost: test.com\r\nAccept: */*\r\n\r\n")
	req, err := lightning.ParseRequest(reqData)
	if err != nil {
		panic(err)
	}
	fmt.Println("=== Request ===")
	fmt.Printf("Method:  %s\n", req.Method)
	fmt.Printf("Path:    %s\n", req.Path)
	fmt.Printf("Version: HTTP/1.%d\n", req.MinorVersion)
	fmt.Printf("Headers (%d):\n", len(req.Headers))
	for _, h := range req.Headers {
		fmt.Printf("  %s: %s\n", h.Name, h.Value)
	}

	fmt.Println()
	respData := []byte("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n\r\nhello")
	resp, err := lightning.ParseResponse(respData)
	if err != nil {
		panic(err)
	}
	fmt.Println("=== Response ===")
	fmt.Printf("Status:  %d\n", resp.StatusCode)
	fmt.Printf("Reason:  %s\n", resp.Reason)
	body := respData[resp.BodyOffset:]
	fmt.Printf("Body:    %s\n", body)
}
