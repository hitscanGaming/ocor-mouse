# Signing keys

The keys in this directory are **development keys, checked into git**. They provide image-integrity validation, NOT security. Anyone with this repo can sign images.

When you move to production:
1. Generate a new key in a hardware-backed KMS (AWS KMS / GCP Cloud KMS / YubiHSM).
2. Replace the `signature_key_file` reference in the MCUboot Kconfig fragment with the KMS-OIDC-fetched key in CI.
3. Rotate device public keys via the next signed DFU image. Pre-production devices using this dev key cannot be field-upgraded to the new key chain (intentional — they're not production).

Until then, treat any release as INTEGRITY-CHECKED but NOT AUTHENTICATED.
