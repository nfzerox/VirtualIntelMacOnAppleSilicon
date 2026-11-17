#pragma once
#import <Foundation/Foundation.h>

// Apple's local Heimdal KDC reads KerberosKeys, independently of ShadowHashData.
// Encode its realm-independent NTLM keyset directly on the host: kvno 1, one
// unsealed arcfour-hmac-md5 EncryptionKey (23), no salt or principal override.
// This is hdb_keyset_aapl / Keys / Key / EncryptionKey from Apple's source:
// https://github.com/apple-oss-distributions/Heimdal/blob/Heimdal-323.92.1/lib/hdb/hdb.asn1
// https://github.com/apple-oss-distributions/Heimdal/blob/Heimdal-323.92.1/lib/asn1/krb5.asn1
// NTLM already requires this exact NT credential; no new password derivation,
// weaker SMB negotiation, host-private framework or guest realm is involved.
static NSData *VIMSMBLocalKeyset(NSData *nt) {
    if (nt.length != 16) return nil;
    const unsigned char header[] = {
        0x30, 0x28,                       // hdb_keyset_aapl SEQUENCE
        0xa1, 0x03, 0x02, 0x01, 0x01,     // [1] kvno INTEGER 1
        0xa0, 0x21, 0x30, 0x1f,           // [0] Keys SEQUENCE OF
        0x30, 0x1d, 0xa1, 0x1b, 0x30, 0x19, // Key / [1] EncryptionKey
        0xa0, 0x03, 0x02, 0x01, 0x17,     // [0] keytype INTEGER 23
        0xa1, 0x12, 0x04, 0x10            // [1] keyvalue OCTET STRING
    };
    NSMutableData *encoded = [NSMutableData dataWithBytes:header length:sizeof(header)];
    [encoded appendData:nt];
    return encoded;
}
