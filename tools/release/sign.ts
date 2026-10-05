// SPDX-License-Identifier: Apache-2.0
// Signs every board's app image with the release key and writes manifest.json,
// install.json and SHA256SUMS next to the assets (spec §4.8, §8; contract §4.1–§4.3).
// Runs only in release.yml's `release` job, inside the `release` environment.
//   GADGET_RELEASE_KEY_R1="$(cat key.pem)" node tools/release/sign.ts --assets out/assets \
//     --meta out/meta --tag v1.1.0 --repo aivsomkar/openmausbot-gadget-sdk \
//     --key-id r1 --key-env GADGET_RELEASE_KEY_R1 --pub keys/release-r1.pub.b64
import { createPrivateKey, createPublicKey, sign } from "node:crypto";
import { readFile, readdir, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { parseArgs } from "node:util";
import { firmwareText } from "../../protocol/lib/identity.ts";
import { verifyP256 } from "../../protocol/lib/verify.ts";
import { fail, runIfMain } from "./cli.ts";
import { readMetas } from "./install-json.ts";
import {
  BOARDS,
  RELEASE_KEY_ID_RE,
  RELEASE_REPO,
  assetNames,
  buildInstallIndex,
  manifestUrl,
  parseReleasePubFiles,
  parseTag,
  sha256Hex,
  sha256sumsText,
  type Manifest,
} from "./lib.ts";

export async function main(argv: string[], env: NodeJS.ProcessEnv = process.env): Promise<number> {
  const { values } = parseArgs({
    args: argv,
    options: {
      assets: { type: "string" },
      meta: { type: "string" },
      tag: { type: "string" },
      repo: { type: "string" },
      "key-id": { type: "string" },
      "key-env": { type: "string" },
      pub: { type: "string" },
      boards: { type: "string", default: BOARDS.join(",") },
    },
    strict: true,
  });
  const { assets, meta, tag, repo, pub } = values;
  const keyId = values["key-id"];
  const keyEnv = values["key-env"];
  if (!assets || !meta || !tag || !repo || !keyId || !keyEnv || !pub) {
    return fail("sign", "--assets, --meta, --tag, --repo, --key-id, --key-env and --pub are required");
  }
  if (repo !== RELEASE_REPO) {
    return fail("sign", `--repo is ${repo}, but manifest URLs are pinned to ${RELEASE_REPO} (contract §4.1)`);
  }
  let version: string;
  let prerelease: boolean;
  try {
    ({ version, prerelease } = parseTag(tag));
  } catch (err) {
    return fail("sign", (err as Error).message);
  }
  if (!RELEASE_KEY_ID_RE.test(keyId)) return fail("sign", `key id ${keyId} is not a release key id (/^r[0-9]+$/)`);

  const pem = env[keyEnv];
  if (!pem) return fail("sign", `${keyEnv} is empty: the release environment holds this secret (docs/release-keys.md)`);
  let privateKey;
  try {
    privateKey = createPrivateKey(pem);
  } catch {
    return fail("sign", `${keyEnv} is not a PEM private key`);
  }
  if (privateKey.asymmetricKeyType !== "ec" || privateKey.asymmetricKeyDetails?.namedCurve !== "prime256v1") {
    return fail("sign", `${keyEnv} is not a P-256 key`);
  }
  const pubFromSecret = createPublicKey(privateKey).export({ type: "spki", format: "der" }).subarray(-65);
  const pubName = pub.split("/").pop() ?? pub;
  let committed: Map<string, string>;
  try {
    committed = parseReleasePubFiles([{ name: pubName, text: await readFile(pub, "utf8") }]);
  } catch (err) {
    return fail("sign", (err as Error).message);
  }
  const committedHex = committed.get(keyId);
  if (committedHex === undefined) return fail("sign", `${pub} is not keys/release-${keyId}.pub.b64`);
  if (pubFromSecret.toString("hex") !== committedHex) {
    return fail("sign", `the public half of ${keyEnv} does not match ${pub}`);
  }
  const pub65 = new Uint8Array(Buffer.from(committedHex, "hex"));

  const boards = values.boards.split(",").filter(Boolean);
  const metas = await readMetas(meta);
  const metaBoards = metas.map((m) => m.board).sort();
  if (JSON.stringify(metaBoards) !== JSON.stringify([...boards].sort())) {
    return fail("sign", `built boards ${metaBoards.join(", ")} differ from --boards ${boards.join(", ")}`);
  }

  const expected = new Set<string>();
  for (const board of boards) for (const name of Object.values(assetNames(board, version))) expected.add(name);
  const present = (await readdir(assets)).sort();
  const stray = present.filter((n) => !expected.has(n));
  const missing = [...expected].filter((n) => !present.includes(n));
  if (stray.length > 0 || missing.length > 0) {
    return fail("sign", `asset folder mismatch: unexpected [${stray.join(", ")}], missing [${missing.join(", ")}]`);
  }

  const manifest: Manifest = { version, boards: {} };
  for (const board of boards) {
    const name = assetNames(board, version).app;
    const image = new Uint8Array(await readFile(join(assets, name)));
    const sha256 = sha256Hex(image);
    const text = firmwareText(board, version, image.length, sha256);
    const der = new Uint8Array(sign("sha256", Buffer.from(text, "utf8"), privateKey));
    if (!verifyP256(pub65, text, der)) return fail("sign", `${board}: the new signature does not verify with ${pub}`);
    manifest.boards[board] = {
      url: manifestUrl(repo, board, version),
      size: image.length,
      sha256,
      sig: Buffer.from(der).toString("base64"),
      key_id: keyId,
    };
  }
  let install;
  try {
    install = buildInstallIndex(version, metas);
  } catch (err) {
    return fail("sign", (err as Error).message);
  }
  await writeFile(join(assets, "manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`);
  await writeFile(join(assets, "install.json"), `${JSON.stringify(install, null, 2)}\n`);
  const files = await Promise.all(
    (await readdir(assets))
      .filter((n) => n !== "SHA256SUMS")
      .map(async (name) => ({ name, sha256: sha256Hex(new Uint8Array(await readFile(join(assets, name)))) })),
  );
  await writeFile(join(assets, "SHA256SUMS"), sha256sumsText(files));
  console.log(`sign: ${tag} (${prerelease ? "prerelease" : "release"}), ${boards.length} boards signed with ${keyId}`);
  return 0;
}

runIfMain(import.meta.url, (argv) => main(argv));
