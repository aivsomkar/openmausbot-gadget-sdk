// SPDX-License-Identifier: Apache-2.0
// Shared CLI plumbing for tools/release: run main() when the file is executed directly.
import { pathToFileURL } from "node:url";

export function runIfMain(metaUrl: string, main: (argv: string[]) => Promise<number>): void {
  const entry = process.argv[1];
  if (entry !== undefined && metaUrl === pathToFileURL(entry).href) {
    main(process.argv.slice(2)).then(
      (code) => {
        process.exitCode = code;
      },
      (err: unknown) => {
        console.error(err instanceof Error ? err.message : String(err));
        process.exitCode = 1;
      },
    );
  }
}

export function fail(tool: string, message: string): number {
  console.error(`${tool}: ${message}`);
  return 1;
}
