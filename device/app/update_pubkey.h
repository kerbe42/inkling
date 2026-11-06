/* The Ed25519 public key that release updates must be signed with (Menu > Check for updates). Its private half is
 * the owner's release key (device/release/make_signing_key.sh; never in the repo); make_release.sh refuses to sign
 * with a key that doesn't match this one. Changing it means every installed copy rejects updates signed with the
 * new key until it has been reinstalled from USB. */
#define UPDATE_PUBKEY_HEX "3e202b94a1a920a68f7508d0b9637e36b6fabf9c8466318ad9f17e2c2129abd1"
