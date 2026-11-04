#!/bin/sh
# Självsignerat certifikat för demoservrarna: server.crt + server.key i samma mapp.
# Klienterna verifierar mot server.crt som enda CA. Giltigt i tio år.
cd "$(dirname "$0")"
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes \
    -keyout server.key -out server.crt -days 3650 \
    -subj "/CN=localhost/O=Ribomation demo" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
openssl x509 -in server.crt -noout -subject -ext subjectAltName -enddate
