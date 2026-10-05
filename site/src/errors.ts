// SPDX-License-Identifier: Apache-2.0
export type InstallerErrorCode =
  | "port_busy"
  | "port_lost"
  | "wrong_chip"
  | "flash_too_small"
  | "board_not_published"
  | "download_failed"
  | "write_failed"
  | "console_write_timeout";

export class InstallerError extends Error {
  readonly code: InstallerErrorCode;
  constructor(code: InstallerErrorCode, message: string) {
    super(message);
    this.code = code;
    this.name = "InstallerError";
  }
}
