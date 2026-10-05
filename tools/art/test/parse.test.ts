// SPDX-License-Identifier: Apache-2.0
import assert from 'node:assert/strict'
import { test } from 'node:test'
import { parseArrayTable, parseCursorBody, parseMotionTable } from '../parse.ts'

const AVATAR = `
export const POOLS = {
  idle: [
    6,
    0
  ],
  'odd-name': [
    3
  ],
}

const BLINK = {
  sleeping: null,
  idle: [
    1000,
    2000
  ],
}

export const MOTION = {
  // a comment line
  idle: { pulse: [0.014, 3600] },
  curious: { sway: [3.4, 1900], tilt: -4 },
}
`

test('array tables: multi-line arrays, quoted keys and null', () => {
  assert.deepEqual(parseArrayTable(AVATAR, 'export const POOLS = {'), { idle: [6, 0], 'odd-name': [3] })
  assert.deepEqual(parseArrayTable(AVATAR, 'const BLINK = {'), { sleeping: null, idle: [1000, 2000] })
})

test('motion table: pairs and scalars', () => {
  assert.deepEqual(parseMotionTable(AVATAR, 'export const MOTION = {'), {
    idle: { pulse: [0.014, 3600] },
    curious: { sway: [3.4, 1900], tilt: -4 },
  })
})

test('a missing table is an error, not an empty result', () => {
  assert.throws(() => parseArrayTable(AVATAR, 'const EXPR_CADENCE = {'), /not found/)
})

test('cursor body from the TS bodies file and the Kotlin bounds', () => {
  const ts = `export const MASCOT_BODIES = {
  cursor: {
    id: "cursor",
    fit: "translate(68.1612 9.8302) scale(0.593918)",
    body: "<path fill=\\"{{GRADIENT}}\\" d=\\"M0 0 C1 1 2 2 3 3 Z\\"/>",
    anchor: { x: 85.54, y: 106.35, scale: 0.791 },
  },
  blob: {
  },
}`
  const kt = `        "cursor" to Body(
            left = 18.7298f,
            top = 0f,
            right = 209.8112f,
            bottom = 228.541f,
        ),`
  assert.deepEqual(parseCursorBody(ts, kt), {
    fit: { tx: 68.1612, ty: 9.8302, scale: 0.593918 },
    path: 'M0 0 C1 1 2 2 3 3 Z',
    anchor: { x: 85.54, y: 106.35, scale: 0.791 },
    bounds: { left: 18.7298, top: 0, right: 209.8112, bottom: 228.541 },
  })
})
