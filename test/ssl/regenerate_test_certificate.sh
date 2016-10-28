#!/bin/sh

set -e

PATH=$PATH:/opt/local/etc/openssl/misc

rm -rf demoCA

printf "\n\n   ======== \x1b[33muse passphrase: \"test\" \x1b[0m=========\n\n"
printf "\n\n   ======== \x1b[33muse common name: \"test\" \x1b[0m=========\n\n"

CA.sh -newca

cp ./demoCA/cacert.pem root_ca_cert.pem
cp ./demoCA/private/cakey.pem root_ca_private.pem

printf "\n\n   ======== \x1b[33muse passphrase: \"test\" \x1b[0m=========\n\n"
printf "\n\n   ======== \x1b[33muse common name: * \x1b[0m=========\n\n"

CA.sh -newreq

cp newkey.pem peer_private_key.pem

CA.sh -sign

cp newcert.pem peer_certificate.pem

openssl dhparam -outform PEM -out dhparams.pem 2048

