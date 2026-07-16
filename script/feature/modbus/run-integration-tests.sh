#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/../common.sh"

generate_modbus_tls_certs() {
	local cert_dir="$1"
	mkdir -p "$cert_dir"

	local ca_key="$cert_dir/ca_key.pem"
	local ca_cert="$cert_dir/ca_cert.pem"
	local sv_key="$cert_dir/server_key.pem"
	local sv_csr="$cert_dir/server.csr"
	local sv_cert="$cert_dir/server_cert.pem"
	local cl_key="$cert_dir/client_key.pem"
	local cl_csr="$cert_dir/client.csr"
	local cl_cert="$cert_dir/client_cert.pem"

	if [[ -f "$ca_cert" && -f "$sv_cert" && -f "$cl_cert" && -f "$sv_key" && -f "$cl_key" ]]; then
		return
	fi

	rm -f "$ca_key" "$ca_cert" "$sv_key" "$sv_csr" "$sv_cert" "$cl_key" "$cl_csr" "$cl_cert" "$cert_dir/ca_cert.srl"

	openssl req -x509 -newkey rsa:2048 -keyout "$ca_key" -out "$ca_cert" -days 30 -nodes -subj '/CN=ModbusTestCA' >/dev/null 2>&1
	openssl genrsa -out "$sv_key" 2048 >/dev/null 2>&1
	openssl req -new -key "$sv_key" -out "$sv_csr" -subj '/CN=127.0.0.1' >/dev/null 2>&1
	openssl x509 -req -in "$sv_csr" -CA "$ca_cert" -CAkey "$ca_key" -CAcreateserial -out "$sv_cert" -days 30 >/dev/null 2>&1
	openssl genrsa -out "$cl_key" 2048 >/dev/null 2>&1
	openssl req -new -key "$cl_key" -out "$cl_csr" -subj '/CN=modbus-client' >/dev/null 2>&1
	openssl x509 -req -in "$cl_csr" -CA "$ca_cert" -CAkey "$ca_key" -CAcreateserial -out "$cl_cert" -days 30 >/dev/null 2>&1
}

generate_modbus_tls_certs /tmp/kmx_modbus_certs_exchange
generate_modbus_tls_certs /tmp/kmx_modbus_certs_reject

test_bin="$(find_test_bin)"

# Run core Modbus integration tests first.
run_with_local_gcc_runtime timeout 25s "$test_bin" "[modbus][integration]~[tls]"

# Run TLS integration tests in isolated processes to avoid cross-test runtime interference.
set +e
run_with_local_gcc_runtime timeout 25s "$test_bin" "modbus tls: mTLS client and server exchange registers"
mtls_status=$?
set -e
if [[ "$mtls_status" -ne 0 && "$mtls_status" -ne 4 ]]; then
	exit "$mtls_status"
fi
if [[ "$mtls_status" -eq 4 ]]; then
	echo "[modbus] mTLS exchange test skipped by Catch2 in current environment"
fi

run_with_local_gcc_runtime timeout 25s "$test_bin" "modbus tls: server rejects client with missing certificate"
