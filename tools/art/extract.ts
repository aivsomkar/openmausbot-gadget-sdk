// SPDX-License-Identifier: Apache-2.0
// Re-pin the Maus art source: read the app's mascot geometry at one commit
// with `git show` (never touching the app's working tree) and write
// source/maus-face.json, source/maus-body-cursor.json, source/states.json
// and source/provenance.json.
//
//   npm run extract -- --app <OpenMausBot checkout> --commit <40-hex sha>
import { execFileSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { parseArrayTable, parseCursorBody, parseMotionTable } from './parse.ts'

const HERE = dirname(fileURLToPath(import.meta.url))
const SOURCE = join(HERE, 'source')
const APP_REPO = 'https://github.com/milind-soni/OpenMausBot'
const FILES = {
  face: 'src/components/cursor-face-data.ts',
  bodies: 'shared/mascot-bodies.ts',
  bodiesKt: 'android/app/src/main/kotlin/com/openmausbot/companion/ui/MausBodies.kt',
  avatar: 'src/components/CursorAvatar.tsx',
}
// The app states the gadget uses (contract §2.15), in ui_maus_state_t order.
const APP_STATES = ['idle', 'listening', 'thinking', 'working', 'sleeping', 'curious', 'notifying', 'alerting']
// Gadget-only state: new, original art and timing (spec §5.5). Not from the app.
const SPEAKING = {
  pool: [19, 6],
  cadence: [3000, 6000],
  blink: [2500, 5000],
  motion: { bob: [1.5, 1800] },
  source: 'gadget-only (original to this SDK)',
}
const SPEAK = { expressions: [19, 6], open: [6, 11, 16], widen: 1.5 }

function arg(name: string): string {
  const i = process.argv.indexOf(name)
  if (i < 0 || !process.argv[i + 1]) {
    console.error('usage: npm run extract -- --app <OpenMausBot checkout> --commit <sha>')
    process.exit(2)
  }
  return process.argv[i + 1]
}

const app = arg('--app')
const commit = arg('--commit')
if (!/^[0-9a-f]{40}$/.test(commit)) {
  console.error('--commit must be a full 40-hex commit id')
  process.exit(2)
}
const show = (path: string): string =>
  execFileSync('git', ['-C', app, 'show', `${commit}:${path}`], { encoding: 'utf8', maxBuffer: 64 << 20 })

const text = {
  face: show(FILES.face),
  bodies: show(FILES.bodies),
  bodiesKt: show(FILES.bodiesKt),
  avatar: show(FILES.avatar),
}

// cursor-face-data.ts is plain TypeScript with erasable types: load it with
// Node's type stripping from a private temp folder (never under node_modules).
const tmp = mkdtempSync(join(tmpdir(), 'omb-art-'))
let face: {
  FACE_BOX: number
  FACE_CENTRE: [number, number]
  MOUTH_STROKE: number
  EXPRESSIONS: [number, number][][][]
  MOUTHS: number[][]
  mouthFrame: (rings: [number, number][][], spec: number[]) => { x: number; y: number; angle: number }
}
try {
  const file = join(tmp, 'cursor-face-data.ts')
  writeFileSync(file, text.face)
  face = await import(pathToFileURL(file).href)
} finally {
  rmSync(tmp, { recursive: true, force: true })
}

const r6 = (v: number): number => Math.round(v * 1e6) / 1e6
const expressions = face.EXPRESSIONS.map((rings, id) => {
  const frame = face.mouthFrame(rings, face.MOUTHS[id])
  return {
    id,
    eyes: rings.map(ring => ring.map(([x, y]) => [r6(x), r6(y)])),
    mouth: face.MOUTHS[id],
    mouth_frame: { x: r6(frame.x), y: r6(frame.y), angle: r6(frame.angle) },
  }
})

const pools = parseArrayTable(text.avatar, 'export const POOLS = {')
const cadence = parseArrayTable(text.avatar, 'const EXPR_CADENCE = {')
const blink = parseArrayTable(text.avatar, 'const BLINK = {')
const motion = parseMotionTable(text.avatar, 'export const MOTION = {')
const states: Record<string, unknown> = {}
for (const name of APP_STATES) {
  const pool = pools[name]
  const cad = cadence[name]
  if (!pool || !cad || !(name in blink) || !motion[name]) throw new Error(`CursorAvatar.tsx: state ${name} is incomplete`)
  states[name] = { pool, cadence: cad, blink: blink[name], motion: motion[name], source: FILES.avatar }
}
states.speaking = SPEAKING

const sha = (s: string): string => createHash('sha256').update(s, 'utf8').digest('hex')
const write = (name: string, value: unknown): void => {
  writeFileSync(join(SOURCE, name), JSON.stringify(value, null, 1) + '\n')
  console.log(`wrote source/${name}`)
}

write('maus-face.json', {
  face_box: face.FACE_BOX,
  face_centre: face.FACE_CENTRE,
  mouth_stroke: face.MOUTH_STROKE,
  expressions,
})
write('maus-body-cursor.json', parseCursorBody(text.bodies, text.bodiesKt))
write('states.json', { states, speak: SPEAK })
write('provenance.json', {
  app_repo: APP_REPO,
  commit,
  license: 'Apache-2.0',
  files: Object.fromEntries((Object.keys(FILES) as (keyof typeof FILES)[]).map(k => [FILES[k], sha(text[k])])),
})
