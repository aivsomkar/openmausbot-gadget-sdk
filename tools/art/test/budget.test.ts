// SPDX-License-Identifier: Apache-2.0
import assert from 'node:assert/strict'
import { test } from 'node:test'
import { BUDGETS, overBudget, totalBytes } from '../budget.ts'

test('budgets are the contract numbers', () => {
  assert.deepEqual(BUDGETS, { s240: { A8: 524288, RGB565A8: 819200 }, s150: { A8: 262144, RGB565A8: 327680 } })
})

test('total bytes come from the generated maus_art_t', () => {
  assert.equal(totalBytes('const maus_art_t maus_art_s150 = {\n  .total_bytes = 140260,\n};\n'), 140260)
  assert.throws(() => totalBytes('nothing here'), /total_bytes/)
})

test('within, at and over the limit', () => {
  assert.equal(overBudget('s240', 'A8', 524288), null)
  assert.match(overBudget('s240', 'A8', 524289)!, /over the 524288-byte budget/)
  assert.equal(overBudget('s150', 'RGB565A8', 300000), null)
  assert.match(overBudget('s999', 'A8', 1)!, /unknown art profile/)
})
