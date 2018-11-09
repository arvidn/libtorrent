#!/bin/sh

set -e

PATH=$PATH:/usr/local/etc/openssl/misc:/usr/lib/ssl/misc

rm -rf demoCA

printf "\n\n   ======== \e[33muse passphrase: \"test\" \e[0m=========\n\n"
printf "\n\n   ======== \e[33muse common name: \"test\" \e[0m=========\n\n"

CA.pl -newca

cp ./demoCA/cacert.pem root_ca_cert.pem
cp ./demoCA/private/cakey.pem root_ca_private.pem

printf "\n\n   ======== \e[33muse passphrase: \"test\" \e[0m=========\n\n"
printf "\n\n   ======== \e[33muse common name: * \e[0m=========\n\n"

CA.pl -newreq

cp newkey.pem peer_private_key.pem

CA.pl -sign

cp newcert.pem peer_certificate.pem

openssl dhparam -outform PEM -out dhparams.pem 2048

printf "\n\nSUCCESS!\n"

