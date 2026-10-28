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

# leaf certificates exercising the subject alternative name and common name
# matching in torrent::verify_peer_cert(). The test torrent is named "temporary"

ext=$(mktemp)
trap 'rm -f "$ext"' EXIT

issue() {
	out=$1
	cn=$2
	san=$3
	{
		echo "basicConstraints=CA:FALSE"
		echo "subjectKeyIdentifier=hash"
		echo "authorityKeyIdentifier=keyid,issuer"
		if [ -n "$san" ]; then echo "subjectAltName=$san"; fi
	} >"$ext"
	openssl req -new -key peer_private_key.pem -passin pass:test \
		-subj "/C=AU/ST=Some-State/O=Internet Widgits Pty Ltd/CN=$cn" \
		| openssl x509 -req -CA root_ca_cert.pem -CAkey root_ca_private.pem \
		-passin pass:test -set_serial "0x$(openssl rand -hex 16)" \
		-days 99999 -extfile "$ext" -out "$out"
}

# the matching DNS name is preceded by a non-DNS, a non-matching and an
# overlong DNS entry
long=$(printf '%0300d' 0 | tr 0 a)
issue san_certificate.pem no-match "email:peer@example.com,DNS:other,DNS:$long,DNS:temporary"
issue san_wildcard_certificate.pem no-match "DNS:*"
issue san_mismatch_certificate.pem no-match "DNS:other"
issue san_mismatch_cn_match_certificate.pem "*" "DNS:other"
issue cn_mismatch_certificate.pem no-match ""

openssl dhparam -outform PEM -out dhparams.pem 4096

printf "\n\nSUCCESS!\n"

