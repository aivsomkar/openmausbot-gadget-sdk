// SPDX-License-Identifier: Apache-2.0
// Text parsers for the OpenMausBot mascot sources (pure; no I/O).

export type Motion = Record<string, number | [number, number]>

export interface CursorBody {
  fit: { tx: number; ty: number; scale: number }
  path: string
  anchor: { x: number; y: number; scale: number }
  bounds: { left: number; top: number; right: number; bottom: number }
}

function num(s: string): number {
  const v = Number(s)
  if (!Number.isFinite(v)) throw new Error(`not a number: ${s}`)
  return v
}

/** The `cursor: { ... }` entry of shared/mascot-bodies.ts. */
export function parseCursorBody(bodiesTs: string, bodiesKt: string): Omit<CursorBody, never> {
  const start = bodiesTs.indexOf('\n  cursor: {')
  if (start < 0) throw new Error('mascot-bodies.ts: no cursor entry')
  const end = bodiesTs.indexOf('\n  },', start)
  const block = bodiesTs.slice(start, end)
  const fit = /fit: "translate\(([-\d.]+) ([-\d.]+)\) scale\(([-\d.]+)\)"/.exec(block)
  const body = /body: ("(?:[^"\\]|\\.)*")/.exec(block)
  const anchor = /anchor: \{ x: ([-\d.]+), y: ([-\d.]+), scale: ([-\d.]+) \}/.exec(block)
  if (!fit || !body || !anchor) throw new Error('mascot-bodies.ts: cursor entry has an unexpected shape')
  const markup = JSON.parse(body[1]) as string
  const d = /d="([^"]+)"/.exec(markup)
  if (!d) throw new Error('mascot-bodies.ts: cursor body has no path')

  const ktStart = bodiesKt.indexOf('"cursor" to Body(')
  if (ktStart < 0) throw new Error('MausBodies.kt: no cursor entry')
  const kt = bodiesKt.slice(ktStart, bodiesKt.indexOf('\n        ),', ktStart))
  const side = (name: string): number => {
    const m = new RegExp(`${name} = ([-\\d.]+)f`).exec(kt)
    if (!m) throw new Error(`MausBodies.kt: cursor has no ${name}`)
    return num(m[1])
  }
  return {
    fit: { tx: num(fit[1]), ty: num(fit[2]), scale: num(fit[3]) },
    path: d[1],
    anchor: { x: num(anchor[1]), y: num(anchor[2]), scale: num(anchor[3]) },
    bounds: { left: side('left'), top: side('top'), right: side('right'), bottom: side('bottom') },
  }
}

/** The object literal that starts at `marker` and ends at the first line that is exactly "}". */
function objectBody(src: string, marker: string): string {
  const start = src.indexOf(marker)
  if (start < 0) throw new Error(`CursorAvatar.tsx: ${marker} not found`)
  const end = src.indexOf('\n}', start)
  return src.slice(start + marker.length, end)
}

/** `name: [a, b, ...]` or `name: null` entries of POOLS / EXPR_CADENCE / BLINK. */
export function parseArrayTable(src: string, marker: string): Record<string, number[] | null> {
  const body = objectBody(src, marker)
  const out: Record<string, number[] | null> = {}
  const re = /^ {2}'?([\w-]+)'?: (null|\[[^\]]*\])/gm
  let m: RegExpExecArray | null
  while ((m = re.exec(body))) {
    out[m[1]] = m[2] === 'null' ? null : m[2].slice(1, -1).split(',').map(s => s.trim()).filter(Boolean).map(num)
  }
  return out
}

/** `name: { bob: [2, 2600], tilt: -4 }` entries of MOTION. */
export function parseMotionTable(src: string, marker: string): Record<string, Motion> {
  const body = objectBody(src, marker)
  const out: Record<string, Motion> = {}
  const re = /^ {2}'?([\w-]+)'?: \{([^}]*)\}/gm
  let m: RegExpExecArray | null
  while ((m = re.exec(body))) {
    const motion: Motion = {}
    const inner = /(\w+): (\[[^\]]*\]|-?[\d.]+)/g
    let f: RegExpExecArray | null
    while ((f = inner.exec(m[2]))) {
      if (f[2].startsWith('[')) {
        const pair = f[2].slice(1, -1).split(',').map(s => num(s.trim()))
        if (pair.length !== 2) throw new Error(`MOTION.${m[1]}.${f[1]}: expected [amount, ms]`)
        motion[f[1]] = [pair[0], pair[1]]
      } else {
        motion[f[1]] = num(f[2])
      }
    }
    out[m[1]] = motion
  }
  return out
}
