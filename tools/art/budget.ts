// SPDX-License-Identifier: Apache-2.0
// npm run budget: every profile's total image bytes within its budget
// (spec 5.5, contract 2.15). Reads the committed firmware/ui/art/<profile>/tables.c.
import { readFileSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

export const BUDGETS: Record<string, { A8: number; RGB565A8: number }> = {
  s240: { A8: 524288, RGB565A8: 819200 },
  s150: { A8: 262144, RGB565A8: 327680 },
}

export function totalBytes(tablesC: string): number {
  const m = /\.total_bytes = (\d+),/.exec(tablesC)
  if (!m) throw new Error('tables.c has no .total_bytes')
  return Number(m[1])
}

/** null when within budget, else the message to print. */
export function overBudget(profile: string, eyeFormat: 'A8' | 'RGB565A8', total: number): string | null {
  const limits = BUDGETS[profile]
  if (!limits) return `unknown art profile ${profile}`
  const limit = limits[eyeFormat]
  return total <= limit ? null : `${profile}: ${total} bytes of art is over the ${limit}-byte budget (${eyeFormat} eyes)`
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const here = dirname(fileURLToPath(import.meta.url))
  const palette = JSON.parse(readFileSync(join(here, 'source', 'palette.json'), 'utf8')) as { eye_format: 'A8' | 'RGB565A8' }
  let failed = false
  for (const profile of Object.keys(BUDGETS)) {
    const total = totalBytes(readFileSync(join(here, '..', '..', 'firmware', 'ui', 'art', profile, 'tables.c'), 'utf8'))
    const problem = overBudget(profile, palette.eye_format, total)
    if (problem) {
      console.error(problem)
      failed = true
    } else {
      console.log(`${profile}: ${total} bytes (budget ${BUDGETS[profile][palette.eye_format]})`)
    }
  }
  process.exit(failed ? 1 : 0)
}
