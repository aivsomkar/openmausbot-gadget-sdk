// SPDX-License-Identifier: Apache-2.0
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { test } from 'node:test'
import { stateDefs, toTenthsPx, unionOfPools, type StatesFile } from '../states.ts'

const states = JSON.parse(readFileSync(new URL('../source/states.json', import.meta.url), 'utf8')) as StatesFile

test('the committed pools cover exactly the 18 expressions of maus_art.h', () => {
  assert.deepEqual(unionOfPools(states), [0, 1, 3, 4, 5, 6, 7, 8, 10, 11, 13, 14, 15, 16, 17, 19, 21, 22])
})

test('pulse becomes a bob of pulse * face_box / 2, added to a bob of the same period', () => {
  const defs = stateDefs(states, 228.541, 240, 228.541)
  const listening = defs.find(d => d.key === 'listening')!
  // bob 2 + pulse 0.012 * 114.2705 = 3.371 face units -> 3.54 px at 240 -> 35 tenths
  assert.deepEqual(listening.bob, [35, 2600])
  const idle = defs.find(d => d.key === 'idle')!
  assert.deepEqual(idle.bob, [17, 3600])
  assert.deepEqual(defs.find(d => d.key === 'alerting')!.jitter, [27, 85])
  assert.deepEqual(defs.find(d => d.key === 'sleeping')!.blink, [0, 0])
  assert.equal(defs.map(d => d.enumName).join(','),
    'UI_MAUS_IDLE,UI_MAUS_LISTENING,UI_MAUS_THINKING,UI_MAUS_WORKING,UI_MAUS_SPEAKING,UI_MAUS_SLEEPING,UI_MAUS_CURIOUS,UI_MAUS_NOTIFYING,UI_MAUS_ALERTING')
})

test('sway, tilt and squash are dropped (translation only in v1)', () => {
  const curious = stateDefs(states, 228.541, 240, 228.541).find(d => d.key === 'curious')!
  assert.deepEqual([curious.bob, curious.jitter, curious.circle], [[0, 0], [0, 0], [0, 0]])
})

test('bob and pulse with different periods are refused', () => {
  const bad = structuredClone(states)
  bad.states.listening.motion = { bob: [2, 2600], pulse: [0.012, 3000] }
  assert.throws(() => stateDefs(bad, 228.541, 240, 228.541), /periods differ/)
})

test('a speaking expression outside every pool is refused', () => {
  const bad = structuredClone(states)
  bad.speak.expressions = [19, 2]
  assert.throws(() => unionOfPools(bad), /speaking expression 2/)
})

test('face units to tenths of a pixel', () => {
  assert.equal(toTenthsPx(228.541, 150, 228.541), 1500)
  assert.equal(toTenthsPx(1, 240, 228.541), 11)
})
