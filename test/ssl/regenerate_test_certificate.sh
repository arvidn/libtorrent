#!/bin/sh

PATH=$PATH:/opt/local/etc/openssl/misc

rm -rf demoCA

printf "\n\n   ======== use passphrase: test =========\n\n"

CA.sh -newca

cp ./demoCA/cacert.pem root_ca_cert.pem
cp ./demoCA/private/cakey.pem root_ca_private.pem

printf "\n\n   ======== use common name: * =========\n\n"

CA.sh -newreq

cp newkey.pem peer_private_key.pem

CA.sh -sign

cp newcert.pem peer_certificate.pem

openssl dhparam -outform PEM -out dhparams.pem 2048

