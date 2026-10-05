# keys/

Public keys for firmware signatures (spec §4.8, §8). Nothing secret lives here.

| File | What |
|---|---|
| `test-t1.key.hex` | The **test** signing key's private scalar (64 lowercase hex). Committed on purpose: the fake host and the tests sign OTA images with it. It is compiled only into simulator and test builds; every board's `sdkconfig.defaults` turns `CONFIG_GADGET_TEST_KEYS` off, and release CI refuses an image that contains it |
| `test-t1.pub.b64` | Its public key: base64 of the 65-byte SEC1 point, then a newline |
| `release-r1.pub.pem` | The release key `r1`'s public half, PEM |
| `release-r1.pub.b64` | The same key as base64 of the 65-byte SEC1 point, then a newline. `firmware/core/src/keys_release.c` is generated from these files |

Release private keys never enter the repository. Each lives only as a secret of the GitHub Actions environment `release` (`GADGET_RELEASE_KEY_R1`), plus Omkar's offline backup. [docs/release-keys.md](../docs/release-keys.md) explains how to create and rotate them.
