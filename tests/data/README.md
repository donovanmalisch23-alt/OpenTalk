# Test data

`test-server.crt` and `test-server.key` are a self-signed EC P-256 certificate and its private key, made **only for automated tests**. The key is public, so it gives no security. Never use it for a real server.

Regenerate with:

```sh
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -keyout test-server.key -out test-server.crt \
  -days 36500 -nodes -subj "/CN=localhost/O=OpenTalk test only" \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
```
