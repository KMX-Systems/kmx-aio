# Mutual TLS (mTLS) Testing

## Overview

The kmx-aio library includes comprehensive Mutual TLS testing coverage with two complementary test suites:

- **Smoke Test**: Quick validation of certificate generation and basic OpenSSL parsing
- **Integration Tests**: Comprehensive scenarios covering chain validation, expiration handling, identity verification, and edge cases

## Smoke Test

### Purpose

Validates that mTLS certificate generation and basic OpenSSL operations work correctly.

### Location

`source/library-test/src/kmx/aio/integration/tls_mtls_smoke_test.cpp`

### Running

```bash
TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
"$TEST_BIN" "[tls][mtls][smoke]"
```

### Validation Checklist

The smoke test performs 7 key validations:

1. **File Existence**: Verifies server certificate, server key, client certificate, and client key files exist
2. **File Sizes**: Validates RSA 2048 keys exceed 1000 bytes and certificates exceed 300 bytes
3. **PEM Headers**: Confirms presence of `BEGIN CERTIFICATE` and `BEGIN RSA PRIVATE KEY` markers
4. **OpenSSL x509 Parsing**: Validates `openssl x509 -text -noout` succeeds on certificates
5. **OpenSSL RSA Validation**: Validates `openssl rsa -check -noout` succeeds on private keys
6. **Content Non-Empty**: Ensures all certificate and key files contain data
7. **Complete mTLS Setup**: Verifies all artifacts are present and valid

### Implementation Details

- **Certificate Generation**: Uses OpenSSL command-line tools to generate certificates:
  - Server: Self-signed X.509 certificate with RSA 2048 key
  - Client: Certificate signed by server's private key
- **Temporary Storage**: Artifacts stored in `/tmp/kmx_mtls_certs/`
- **Shell Safety**: Uses `shell_quote()` to safely escape arguments to OpenSSL commands
- **Format Agnostic**: Validates via tool exit codes, not text patterns (handles OpenSSL version variations)

## Integration Tests

### Purpose

Comprehensive testing of mTLS certificate scenarios and edge cases encountered in real deployments.

### Location

`source/library-test/src/kmx/aio/integration/tls_mtls_integration_test.cpp`

### Running

```bash
TEST_BIN="$(find debug -type f -name kmx-aio-test | head -n 1)"
"$TEST_BIN" "[tls][mtls][integration]"
```

### Test Scenarios

#### 1. Certificate Chain Validation

Tests the complete mTLS certificate chain:
- Server certificate and private key generation
- Client certificate generation and signing
- File existence and PEM format validation
- OpenSSL parsing of all components

#### 2. Expired Certificate Handling

Validates behavior with time-sensitive certificates:
- Generates certificates with 1-day validity window
- Validates OpenSSL can detect expiration
- Tests handling of certificates beyond validity period

#### 3. Certificate Identity Verification

Tests Common Name (CN) field extraction and verification:
- Extracts CN from certificate subjects
- Validates CN contains expected values
- Tests certificate identity consistency

#### 4. Multiple Certificate Sets

Validates independent certificate set generation:
- Generates 3 independent mTLS certificate pairs
- Verifies uniqueness (no collisions)
- Tests isolation between sets
- Validates all sets have correct structure

#### 5. Certificate File Operations

Tests file I/O and resource management:
- Validates file sizes meet minimum requirements
- Tests read operations on certificate files
- Verifies proper file handling and cleanup

#### 6. Certificate Format Validation

Comprehensive PEM format and OpenSSL parsing:
- Validates PEM headers and structure
- Tests x509 certificate parsing
- Tests RSA private key parsing
- Verifies all cryptographic operations succeed

## Certificate Artifacts

### Location

All temporary mTLS certificates are stored in:

```
/tmp/kmx_mtls_certs/
```

### Generated Files

For each certificate set:
- `server_cert.pem` - Server's self-signed X.509 certificate
- `server_key.pem` - Server's RSA 2048 private key
- `client_cert.pem` - Client certificate signed by server
- `client_key.pem` - Client's RSA 2048 private key
- `client.csr` - Client certificate signing request (temporary)

### Cleanup

Test suites automatically manage artifact lifecycle:
- Creates certificates before validation
- Reads and validates all files
- Cleans up temporary artifacts after test completion

## Test Tags

Both suites use Catch2 tags for filtering:

- `[tls]` - TLS-related tests
- `[mtls]` - Mutual TLS specific tests
- `[smoke]` - Smoke test suite
- `[integration]` - Integration test suite
- `[slow]` - Slow-running tests

### Running Specific Filters

```bash
# All mTLS tests
"$TEST_BIN" "[tls][mtls]"

# Smoke tests only
"$TEST_BIN" "[tls][mtls][smoke]"

# Integration tests only
"$TEST_BIN" "[tls][mtls][integration]"

# All TLS tests (including mTLS)
"$TEST_BIN" "[tls]"
```

## Building

Both test suites are included in the standard test binary build:

```bash
cd source
qbs build -f source.qbs config:debug --products kmx-aio-test -j"$(nproc)"
cd ..
```

No additional build flags are required; mTLS tests compile as part of the default integration test suite.

## Performance

- **Smoke Test**: ~1-2 seconds (basic validation only)
- **Integration Tests**: ~3-5 seconds (6 comprehensive scenarios)
- Both tagged `[slow]` for CI integration and flake detection

## Dependencies

Both test suites depend on:

- **OpenSSL**: Command-line `openssl` tool (x509 and rsa subcommands)
- **C++26 Standard Library**: `<filesystem>`, `<fstream>`, `<chrono>`, `<cstdlib>`
- **Catch2 v3.15.1**: Test framework with tag-based filtering

## Troubleshooting

### Certificate Generation Fails

If OpenSSL commands fail:
1. Verify `openssl` is in PATH: `which openssl`
2. Check `/tmp/kmx_mtls_certs/` directory is writable
3. Ensure sufficient disk space for temporary files

### OpenSSL Parsing Fails

If x509 or RSA validation fails:
1. Verify OpenSSL version supports required features
2. Check certificate format is valid PEM
3. Ensure private keys are in PKCS#1 format (RSA PRIVATE KEY)

### Tests Not Running

If mTLS tests don't appear in filter results:
1. Verify `[tls][mtls]` tags are present in test output
2. Ensure test binary is built with debug configuration
3. Check integration tests are compiled (requires `project.enable_quic:true`)

## Integration with CI

Both test suites run automatically in:

```bash
bash scripts/run-integration-tests.sh
```

The full integration test script handles:
- Build configuration
- Temporary file setup
- Test execution with proper timeouts
- Cleanup of artifacts

## Future Enhancements

Potential areas for expansion:

- Integration with actual TLS stream connections
- Certificate revocation testing
- Chain-of-trust validation with multiple CAs
- Hardware security module (HSM) support testing
- Performance benchmarking under load
