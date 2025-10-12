Place TLS credentials here if you choose custom PEMs via Kconfig:

- Root CA: `ca.pem` (PEM-encoded). Required if `IAQ_MQTT_TLS_TRUST_CA_PEM=y`.
- Client certificate: `client.crt.pem` (PEM). Required if `IAQ_MQTT_MTLS_ENABLE=y`.
- Client private key: `client.key.pem` (PEM). Required if `IAQ_MQTT_MTLS_ENABLE=y`.

Notes
- For public brokers (Let's Encrypt, etc.), prefer the certificate bundle option.
- File names are fixed. Do not change without updating the CMakeLists.txt.
- Keys should NOT be password-protected at this time.
