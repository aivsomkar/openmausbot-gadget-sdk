// SPDX-License-Identifier: Apache-2.0
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { test } from 'node:test'
import { bodySvg, eyesSvg, mouthSvg, speakMouthSvg, type Body, type Face, type Palette } from '../svg.ts'

const read = (n: string): unknown => JSON.parse(readFileSync(new URL(`../source/${n}`, import.meta.url), 'utf8'))
const face = read('maus-face.json') as Face
const body = read('maus-body-cursor.json') as Body
const palette = read('palette.json') as Palette

test('every layer shares the body-tight viewBox', () => {
  const vb = 'viewBox="18.73 0 191.081 228.541"'
  assert.ok(bodySvg(body, palette).includes(vb))
  assert.ok(eyesSvg(face, body, face.expressions[6], 1, '#ffffff').includes(vb))
  assert.ok(mouthSvg(face, body, face.expressions[6], '#ffffff').includes(vb))
})

test('the body gradient runs top-right to bottom-left with the app palette', () => {
  const svg = bodySvg(body, palette)
  assert.match(svg, /x1="1" y1="0" x2="0" y2="1"/)
  assert.match(svg, /offset="0" stop-color="#8cd1b3".*offset="0.55" stop-color="#009957".*offset="1" stop-color="#005932"/)
})

test('a closed blink squashes each eye to 4 % of its height', () => {
  const ys = (svg: string): number[] =>
    [...svg.matchAll(/[ML](-?[\d.]+) (-?[\d.]+)/g)].slice(0, 48).map(m => Number(m[2]))
  const open = ys(eyesSvg(face, body, face.expressions[6], 1, '#fff'))
  const shut = ys(eyesSvg(face, body, face.expressions[6], 0.04, '#fff'))
  const span = (v: number[]): number => Math.max(...v) - Math.min(...v)
  assert.ok(Math.abs(span(shut) / span(open) - 0.04) < 0.01)
})

test('speaking mouths are closed lenses filled with the shadow green', () => {
  const svg = speakMouthSvg(face, body, face.expressions[19], 16, 3, 1.5, '#ffffff', '#005932')
  assert.match(svg, /d="M[^"]+ Q[^"]+ Q[^"]+ Z" fill="#005932" stroke="#ffffff"/)
})
