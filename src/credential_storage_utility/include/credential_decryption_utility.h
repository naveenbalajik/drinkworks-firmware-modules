/**
 * @file credential_decryption_utility.h
 *
 * Created on: May 20, 2020
 * 		Author: nick.weber
 */

#ifndef CREDENTIALDECRYPTIONUTILITY_H
#define CREDENTIALDECRYPTIONUTILITY_H


#define CLAIM_CERT_ENCRYPTED_BYTE_LENGTH		(1232)

#define CLAIM_PRIVKEY_ENCRYPTED_BYTE_LENGTH		(1680)

#define CODE_SIGN_CERT_ENCRYPTED_BYTE_LENGTH	(576)

/**
 * @brief Array to hold claim certificate after decryption
 */
unsigned char plaintextClaimCert[CLAIM_CERT_ENCRYPTED_BYTE_LENGTH];

/**
 * @brief Claim certificate length after decryption
 */
unsigned int claimCertLength;

/**
 * @brief Array to hold claim private key after decryption
 */
unsigned char plaintextClaimPrivKey[CLAIM_PRIVKEY_ENCRYPTED_BYTE_LENGTH];

/**
 * @brief Claim private key length after decryption
 */
unsigned int claimPrivKeyLength;

/**
 * @brief Array to hold code signing certificate after decryption
 */
unsigned char plaintextCodeSignCert[CODE_SIGN_CERT_ENCRYPTED_BYTE_LENGTH];

/**
 * @brief Code signing cert length after decryption
 */
unsigned int codeSignCertLength;


int32_t credentialUtility_decryptCredentials(void);

#endif // !CREDENTIALDECRYPTIONUTILITY_H
