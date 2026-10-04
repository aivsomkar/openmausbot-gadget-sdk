// SPDX-License-Identifier: Apache-2.0
// Fixed values from docs/plans/00-interfaces.md §1.7. Computed with @noble/curves 2.4.0
// (lowS: false) and checked with node:crypto. The generator must reproduce them byte for byte.
export const RFC_PRIVATE_HEX = "c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721";
export const RFC_PUBKEY_B64 = "BGD+1LolWp0xyWHrdMY1bWjASbiSO2H6bOZpYi5g8p+2eQP+EAi4vJmkGunpVii8ZPLxsgwtfp9Rd6PClNRGIpk=";
export const RFC_ID = "gad_b18b86ce1389e46d";
export const RFC_SAMPLE_DER_HEX =
  "3046022100efd48b2aacb6a8fd1140dd9cd45e81d69d2c877b56aaf991c34d0ea84eaf3716022100f7cb1c942d657c41d436c7a1b6e29f65f3e900dbb9aff4064dc4ab2f843acda8";
export const PINNED_NONCE_B64 = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=";
export const PINNED_HOST_ID = "000102030405060708090a0b0c0d0e0f";
export const PROVE_TEXT =
  "openmausbot-gadget/1\nprove\ngad_b18b86ce1389e46d\nAAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=\n000102030405060708090a0b0c0d0e0f";
export const PROVE_DER_HEX =
  "3046022100f0c4fbe24029d797b16b36dcc0d05fb7a8b9df8c5ca4b27ad99d820d4d8a664202210087d087e1c83ab59e8feebee63a40b423c5af81956ceca8033264098abf44e35a";
export const PROVE_SIG_B64 = "MEYCIQDwxPviQCnXl7FrNtzA0F+3qLnfjFyksnrZnYINTYpmQgIhAIfQh+HIOrWej+6+5jpAtCPFr4GVbOyoAzJkCYq/RONa";
export const LOW_S_HOST_ID = "0123456789abcdef0123456789abcdef";
export const SHORT_DER_MESSAGE = "openmausbot-gadget/1 der-short 13";
export const SHORT_DER_HEX =
  "3043022052c1af44f658bb58a5b434a96d609052855835feca85b011cdfcb4159f52c37e021f2d466a13caea297fa0c97498ce12ed77ea25e9895415c4bb6ebe64b2a049cb";
export const T1_SEED_TEXT = "openmausbot-gadget/1 test release key t1";
export const T1_PRIVATE_HEX = "274b871e29523ce21208177b90a0a3bd6a169cb84cf867236adc68f3d90fc0c8";
export const T1_PUBKEY_B64 = "BIUTH1UeVJw2VXn0Ehdhd8apNOHmB6VvjV512SxIbCCXfUhvlLRTuOTLHtrsVAbIx06JhMoOMbC3ic73hZpxMwg=";
export const FIRMWARE_TEXT =
  "openmausbot-gadget/1\nfirmware\namoled-175c\n1.1.0\n1234567\ne3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
export const FIRMWARE_T1_DER_HEX =
  "3046022100de2dfa125231a739f0b1b27e2ec085e4027b59e4486978457ffccb95b3895f2d022100ef00bd5c4177cb97ba4a4f4036046f6a20b3c896c5ab1124720b54afe6179744";
/** P-256 group order n, for low-S/high-S conversions in tests and the generator. */
export const P256_N = 0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551n;
