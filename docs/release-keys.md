# Release keys and the release workflow

Official firmware images are signed with a P-256 release key (spec §4.8, §8). Gadgets accept an over-the-air update only when its signature verifies with a release key compiled into their firmware, and MausBot checks the same signature before it offers the update. This page is Omkar's one-time setup and the rotation procedure. Until step 3 is done, `release.yml` fails on purpose: the release key table is empty.

## 1. GitHub settings (once)

1. **Pages:** Settings → Pages → Build and deployment → Source: **GitHub Actions**.
2. **Release environment:** Settings → Environments → New environment → `release`.
   - Required reviewers: Omkar (and anyone else who may approve a release).
   - Deployment branches and tags: **Selected branches and tags** → add a **tag** rule `v*`. Without it, tag runs cannot use the environment.
3. Leave the `github-pages` environment on its default (main only). `release.yml` dispatches `pages.yml` on `main`, so the installer redeploys from main.

## 2. Create the r1 key (once, on a trusted computer)

Use OpenSSL 3 (`brew install openssl@3`; macOS's LibreSSL also works):

```sh
cd /path/to/openmausbot-gadget-sdk
umask 077
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out ~/release-r1.pem
openssl pkey -in ~/release-r1.pem -pubout -out keys/release-r1.pub.pem
openssl pkey -in ~/release-r1.pem -pubout -outform DER | tail -c 65 | openssl base64 -A > keys/release-r1.pub.b64
echo >> keys/release-r1.pub.b64
gh secret set GADGET_RELEASE_KEY_R1 --env release --repo aivsomkar/openmausbot-gadget-sdk < ~/release-r1.pem
```

Move `~/release-r1.pem` to offline storage (a password manager or an encrypted drive), then delete the local copy. Anyone with it can sign firmware that every gadget accepts.

## 3. Compile the key into the firmware

```sh
node tools/release/gen-release-keys.ts
node tools/release/check-keys.ts
```

`check-keys.ts` must print `check-keys: release keys r1; test table empty`. Commit `keys/release-r1.pub.pem`, `keys/release-r1.pub.b64` and `firmware/core/src/keys_release.c` together.

MausBot needs the same public key: copy the one line of `keys/release-r1.pub.b64` into OpenMausBot's `companion/src/gadget/release-keys.ts` (`RELEASE_KEYS`, plan P4b).

## 4. Cut a release

0. **Before the first public release only:** confirm that the mascot expression geometry (`src/components/cursor-face-data.ts` in OpenMausBot at the pinned commit `6dd4403d8fbbbd5c17169724cb2a529f11d7543e`, which `tools/art` generates the Maus art from) is project-owned (spec §11). This is a pre-publish check, not a build step: nothing in CI checks it.
1. Make sure `main` is green.
2. Tag and push. A first dry run as a prerelease is a good idea:

   ```sh
   git tag v1.0.0-rc.1 && git push origin v1.0.0-rc.1
   ```

3. In the Actions tab, approve the `release` job when it waits for review. It signs the images, publishes the release with the per-board assets, `manifest.json`, `install.json` and `SHA256SUMS`, and dispatches `pages.yml`.
4. Tags with a `-` are prereleases: they never become "latest", so neither MausBot nor the installer sees them. Tag `v1.0.0` when the prerelease looks right.

What the workflow refuses: a tag that is not `v<major>.<minor>.<patch>[-<pre>]`, a version ending in `-dev`, an image whose embedded version is not the tag's, an app larger than the 6 MiB OTA slot, a built partition table that differs from `firmware/ports/esp32/partitions/16mb.csv` or a flashed part that reaches into `nvs` or `phy_init` (a reinstall would wipe the gadget's pairing), an image that contains the test key `t1` or lacks a release key, a secret whose public half differs from `keys/release-r1.pub.b64`, a repository other than `aivsomkar/openmausbot-gadget-sdk` (the manifest URLs are pinned to it; a rename or transfer means changing `RELEASE_REPO` here and `RELEASE_URL_PREFIX` in MausBot together), and a release key table that is empty or holds a non-`r` id.

## Rotating to a new key

Gadgets trust every key in their firmware's table, so rotate in two releases:

1. Create `r2` exactly as in step 2 (secret `GADGET_RELEASE_KEY_R2`, files `keys/release-r2.pub.*`), run step 3 (the table now holds r1 and r2), and add r2 to OpenMausBot's `RELEASE_KEYS`. Release as usual: this release is still signed with r1, and teaches gadgets r2.
2. Once most gadgets run that release, change the `release` job in `.github/workflows/release.yml` to `--key-id r2 --key-env GADGET_RELEASE_KEY_R2 --pub keys/release-r2.pub.b64` with `GADGET_RELEASE_KEY_R2` in its `env`, and release again.

## If a key leaks

Current gadgets trust only the keys in their firmware, so the leaked key has to sign one last release that drops it:

1. Create `r2` as in step 2 (secret `GADGET_RELEASE_KEY_R2`, files `keys/release-r2.pub.*`).
2. Retire r1: `mkdir -p keys/retired && git mv keys/release-r1.pub.pem keys/release-r1.pub.b64 keys/retired/`, then run step 3. The table now holds only r2; `gen-release-keys.ts`, `check-keys.ts` and `collect.ts` read only the files at the top of `keys/` and skip `keys/retired/`.
3. In `.github/workflows/release.yml`, point the `release` job's `--pub` at `keys/retired/release-r1.pub.b64` and release. This release is still signed with r1, so gadgets and MausBot accept it, and it teaches gadgets to trust only r2. Add r2 to OpenMausBot's `RELEASE_KEYS` in the same round.
4. Switch the `release` job to `--key-id r2 --key-env GADGET_RELEASE_KEY_R2 --pub keys/release-r2.pub.b64` (with `GADGET_RELEASE_KEY_R2` in its `env`), delete the `GADGET_RELEASE_KEY_R1` secret, and remove r1 from OpenMausBot's `RELEASE_KEYS`.

v1 has no revocation: until a gadget installs the release from step 3, whoever holds the leaked key can sign images it accepts. Ask users to update, or to reflash over USB.
