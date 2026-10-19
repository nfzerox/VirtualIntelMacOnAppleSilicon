#!/usr/bin/env python3
"""Create a local, persistent code-signing identity for development builds.

macOS may ask once to approve the certificate/keychain change. Trust is limited
to code signing in this user's keychain. No Apple account or host security
changes are needed. Private builds can restore the shared identity from
Source/Private; the public source exporter excludes that entire directory.
"""
import argparse
import hashlib
import os
from pathlib import Path
import plistlib
import secrets
import subprocess
import tempfile

source = Path(__file__).resolve().parents[1]
receipt = source / "Build/DevelopmentSigning.plist"
shared = source / "Private/DevelopmentSigning"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--export-shared', action='store_true',
                    help='explicitly export only the configured identity into this private repository')
args = parser.parse_args()

def helper():
    output = source / 'Build/development-signing-key'
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['xcrun', 'clang', '-fobjc-arc', '-Wno-deprecated-declarations',
                    str(source/'scripts/development-signing-key.m'), '-framework',
                    'Foundation', '-framework', 'Security', '-o', str(output)], check=True)
    return output

def identities():
    return subprocess.check_output(['security', 'find-identity', '-v', '-p', 'codesigning'], text=True)

if args.export_shared:
    if not receipt.is_file():
        parser.error('Configure the existing signing identity first; export never creates or replaces an identity.')
    if shared.exists():
        parser.error('The shared identity already exists; refusing to replace it.')
    identity = plistlib.loads(receipt.read_bytes())['Identity']
    if identity not in identities():
        parser.error('The configured identity is not available in the Keychain.')
    shared.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='signing-', dir=shared.parent) as temporary:
        directory = Path(temporary)
        password = secrets.token_hex(32)
        archive = directory/'Identity.p12'
        subprocess.run([str(helper()), 'export', identity, str(archive)], check=True,
                       env=dict(os.environ, VIM_KEY_EXPORT_PASSWORD=password))
        certificate = directory/'Identity.p12.cer'
        der = certificate.read_bytes()
        if hashlib.sha1(der).hexdigest().upper() != identity:
            raise SystemExit('Exported certificate does not match the configured identity.')
        # Do not carry personal certificate subject/issuer metadata between hosts.
        fields = subprocess.check_output(['openssl', 'x509', '-inform', 'DER', '-in',
            str(certificate), '-noout', '-subject', '-issuer', '-nameopt', 'RFC2253'], text=True)
        normalized = [line.split('=', 1)[0]+'='+line.split('=', 1)[1].strip()
                      for line in fields.splitlines() if line]
        if normalized != ['subject=CN=Virtual Intel Mac Local Development',
                          'issuer=CN=Virtual Intel Mac Local Development']:
            raise SystemExit('Certificate contains unexpected identity metadata; not checking it in.')
        (directory/'Password.txt').write_text(password+'\n')
        (directory/'Identity.plist').write_bytes(plistlib.dumps({
            'Identity': identity, 'Name': 'Virtual Intel Mac Local Development'}))
        for path in directory.iterdir(): path.chmod(0o600)
        subprocess.run([str(helper()), 'verify', identity, str(archive)], check=True,
                       env=dict(os.environ, VIM_KEY_EXPORT_PASSWORD=password))
        directory.rename(shared)
    print('Exported only the configured development identity. Keep Source/Private private.')
    raise SystemExit(0)

# Restoring the shared identity is automatic on a new private-repository builder.
# The private repository's checked-in identity is authoritative. A stale local
# build receipt is not authority to rotate the shared key or block a new host.
if (shared/'Identity.plist').is_file():
    metadata = plistlib.loads((shared/'Identity.plist').read_bytes())
    identity = metadata['Identity']
    if identity not in identities():
        password = (shared/'Password.txt').read_text().strip()
        subprocess.run([str(helper()), 'import', identity, str(shared/'Identity.p12')], check=True,
                       env=dict(os.environ, VIM_KEY_EXPORT_PASSWORD=password))
        keychain = subprocess.check_output(['security', 'default-keychain', '-d', 'user'], text=True).strip().strip('"')
        subprocess.run(['security', 'add-trusted-cert', '-r', 'trustRoot', '-p', 'codeSign',
                        '-k', keychain, str(shared/'Identity.p12.cer')], check=True)
        if identity not in identities():
            raise SystemExit('Imported identity is not available for code signing; check Keychain trust.')
    receipt.parent.mkdir(parents=True, exist_ok=True)
    receipt.write_bytes(plistlib.dumps(metadata))
    print(f'Shared development signing is configured: {identity}')
    raise SystemExit(0)

if receipt.exists():
    identity = plistlib.loads(receipt.read_bytes())["Identity"]
    identities = subprocess.check_output(["security", "find-identity", "-v", "-p", "codesigning"], text=True)
    if identity not in identities:
        raise SystemExit("The configured identity is unavailable. Restore its Keychain certificate/private key before rebuilding; silently replacing it would invalidate existing permissions.")
    print(f"Development signing is already configured: {identity}")
    raise SystemExit(0)

keychain = subprocess.check_output(["security", "default-keychain", "-d", "user"],
                                  text=True).strip().strip('"')
name = "Virtual Intel Mac Local Development"
environment = dict(os.environ, VIM_KEY_EXPORT_PASSWORD=secrets.token_hex(32))
with tempfile.TemporaryDirectory(prefix="vim-signing-") as temporary:
    directory = Path(temporary)
    config = directory / "certificate.cnf"
    config.write_text("""[req]
prompt = no
distinguished_name = subject
x509_extensions = extensions
[subject]
CN = Virtual Intel Mac Local Development
[extensions]
basicConstraints = critical,CA:FALSE
keyUsage = critical,digitalSignature
extendedKeyUsage = critical,codeSigning
subjectKeyIdentifier = hash
""")
    key, cert, archive = (directory / filename for filename in
                          ("private.pem", "certificate.pem", "identity.p12"))
    subprocess.run(["openssl", "req", "-new", "-x509", "-newkey", "rsa:3072",
                    "-nodes", "-days", "3650", "-config", str(config),
                    "-keyout", str(key), "-out", str(cert)], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    key.chmod(0o600)
    subprocess.run(["openssl", "pkcs12", "-export", "-inkey", str(key),
                    "-in", str(cert), "-name", name, "-out", str(archive),
                    "-passout", "env:VIM_KEY_EXPORT_PASSWORD"], check=True, env=environment)
    subprocess.run(["security", "import", str(archive), "-k", keychain,
                    "-P", environment["VIM_KEY_EXPORT_PASSWORD"],
                    "-T", "/usr/bin/codesign"], check=True)
    subprocess.run(["security", "add-trusted-cert", "-r", "trustRoot", "-p",
                    "codeSign", "-k", keychain, str(cert)], check=True)
    der = subprocess.check_output(["openssl", "x509", "-in", str(cert), "-outform", "DER"])
    identity = hashlib.sha1(der).hexdigest().upper()
    receipt.parent.mkdir(parents=True, exist_ok=True)
    receipt.write_bytes(plistlib.dumps({"Identity": identity, "Name": name,
                                       "Keychain": keychain}))
print(f"Persistent local development identity configured: {identity}")
