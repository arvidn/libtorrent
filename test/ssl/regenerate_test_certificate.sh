#!/bin/sh

set -e

printf "\n\n   ======== \e[33muse common name: * \e[0m=========\n\n"

openssl req -newkey rsa:4096 -nodes -keyout server_key.pem -x509 -days 99999 -out server_cert.pem
cat server_key.pem server_cert.pem >server.pem

rm -rf demoCA
mkdir demoCA
mkdir demoCA/private
mkdir demoCA/newcerts
touch demoCA/index.txt

printf "\n\n   ======== \e[33muse common name: \"test\" \e[0m=========\n\n"

openssl req  -new -keyout ./demoCA/private/cakey.pem -out ./demoCA/careq.pem -passout pass:test
openssl ca  -create_serial -out ./demoCA/cacert.pem -days 99999 -batch -keyfile ./demoCA/private/cakey.pem -selfsign -extensions v3_ca -passin pass:test -infiles ./demoCA/careq.pem

cp ./demoCA/cacert.pem root_ca_cert.pem
cp ./demoCA/private/cakey.pem root_ca_private.pem

printf "\n\n   ======== \e[33muse common name: * \e[0m=========\n\n"

openssl req  -new  -keyout newkey.pem -out newreq.pem -passin pass:test -passout pass:test

cp newkey.pem peer_private_key.pem

openssl ca  -policy policy_anything -out newcert.pem -days 99999 -passin pass:test -infiles newreq.pem

cp newcert.pem peer_certificate.pem

openssl dhparam -outform PEM -out dhparams.pem 4096

printf "\n\nSUCCESS!\n"

