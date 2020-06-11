#!/bin/sh

set -e

cp peer_certificate.pem invalid_peer_certificate.pem
cp peer_private_key.pem invalid_peer_private_key.pem

printf "\n\n   ======== \e[33muse common name: * \e[0m=========\n\n"

openssl req -newkey rsa:4096 -nodes -keyout server_key.pem -x509 -days 1097 -out server_cert.pem
cat server_key.pem server_cert.pem >server.pem

PATH=$PATH:/usr/local/etc/openssl/misc:/usr/lib/ssl/misc

rm -rf demoCA

printf "\n\n   ======== \e[33muse passphrase: \"test\" \e[0m=========\n\n"
printf "\n\n   ======== \e[33muse common name: \"test\" \e[0m=========\n\n"

CA.pl -newca -extra-cmd rsa:4096

cp ./demoCA/cacert.pem root_ca_cert.pem
cp ./demoCA/private/cakey.pem root_ca_private.pem

printf "\n\n   ======== \e[33muse passphrase: \"test\" \e[0m=========\n\n"
printf "\n\n   ======== \e[33muse common name: * \e[0m=========\n\n"

CA.pl -newreq -extra-cmd rsa:4096

cp newkey.pem peer_private_key.pem

CA.pl -sign

cp newcert.pem peer_certificate.pem

openssl dhparam -outform PEM -out dhparams.pem 4096

printf "\n\nSUCCESS!\n"

