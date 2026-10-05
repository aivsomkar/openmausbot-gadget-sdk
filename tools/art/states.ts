// SPDX-License-Identifier: Apache-2.0
// states.json -> maus_state_def_t rows (pure; no I/O).
//
// Motion is translation only in v1 (spec §5.5): bob, jitter and circle are
// kept; the app's breathing pulse becomes a vertical bob of
// pulse * face_box / 2 face units; sway, tilt, squash, enter and settle are
// dropped. Amplitudes are emitted as integers in 0.1 px (contract D17).

export type Pair = [number, number]
export interface StateSource {
  pool: number[]
  cadence: Pair
  blink: Pair | null
  motion: Record<string, number | Pair>
  source: string
}
export interface StatesFile {
  states: Record<string, StateSource>
  speak: { expressions: number[]; open: number[]; widen: number }
}
export interface StateDef {
  key: string
  enumName: string
  pool: number[]
  cad: Pair
  blink: Pair
  bob: Pair
  jitter: Pair
  circle: Pair
}

/** ui_maus_state_t order, without UI_MAUS_NONE. */
export const STATE_ORDER = ['idle', 'listening', 'thinking', 'working', 'speaking', 'sleeping', 'curious', 'notifying', 'alerting'] as const

export function unionOfPools(file: StatesFile): number[] {
  const set = new Set<number>()
  for (const key of STATE_ORDER) {
    const s = file.states[key]
    if (!s) throw new Error(`states.json: missing state ${key}`)
    for (const e of s.pool) set.add(e)
  }
  for (const e of file.speak.expressions) {
    if (!set.has(e)) throw new Error(`states.json: speaking expression ${e} is in no pool`)
  }
  return [...set].sort((a, b) => a - b)
}

function pair(v: number | Pair | undefined, what: string): Pair | undefined {
  if (v === undefined) return undefined
  if (!Array.isArray(v) || v.length !== 2) throw new Error(`${what}: expected [amount, period_ms]`)
  return v
}

/** Face units -> 0.1 px at this profile's height. */
export function toTenthsPx(faceUnits: number, profileHeight: number, bodyHeightFu: number): number {
  return Math.round(faceUnits * profileHeight / bodyHeightFu * 10)
}

export function stateDefs(file: StatesFile, faceBox: number, profileHeight: number, bodyHeightFu: number): StateDef[] {
  return STATE_ORDER.map(key => {
    const s = file.states[key]
    const what = `states.json ${key}`
    const bob = pair(s.motion.bob, `${what} bob`)
    const pulse = pair(s.motion.pulse, `${what} pulse`)
    const jitter = pair(s.motion.jitter, `${what} jitter`)
    const circle = pair(s.motion.circle, `${what} circle`)
    let bobFu = 0
    let bobMs = 0
    if (bob) { bobFu = bob[0]; bobMs = bob[1] }
    if (pulse) {
      if (bob && bob[1] !== pulse[1]) throw new Error(`${what}: bob and pulse periods differ`)
      bobFu += pulse[0] * faceBox / 2
      bobMs = pulse[1]
    }
    const px = (fu: number): number => toTenthsPx(fu, profileHeight, bodyHeightFu)
    return {
      key,
      enumName: `UI_MAUS_${key.toUpperCase()}`,
      pool: s.pool,
      cad: s.cadence,
      blink: s.blink ?? [0, 0],
      bob: [px(bobFu), bobMs],
      jitter: jitter ? [px(jitter[0]), jitter[1]] : [0, 0],
      circle: circle ? [px(circle[0]), circle[1]] : [0, 0],
    }
  })
}
