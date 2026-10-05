// SPDX-License-Identifier: Apache-2.0
// npm run art: source/*.json -> firmware/ui/art/{maus_art.h, maus_art.c, s240/*.c, s150/*.c}
// plus review previews in tools/art/out/<profile>/ (gitignored).
import { mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { createRequire } from 'node:module'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { C_HEADER, cImage, toLvImage, trim, type ColorFormat, type LvImage, type Rgba } from './lvgl.ts'
import { bodySvg, eyesSvg, mouthSvg, previewSvg, speakMouthSvg, type Body, type Face, type Palette } from './svg.ts'
import { stateDefs, unionOfPools, type StatesFile } from './states.ts'

const require = createRequire(import.meta.url)
const { Resvg } = require('@resvg/resvg-js') as { Resvg: new (svg: string, opts: object) => { render(): { asPng(): Buffer } } }
const { PNG } = require('pngjs') as { PNG: { sync: { read(b: Buffer): { width: number; height: number; data: Buffer } } } }

const HERE = dirname(fileURLToPath(import.meta.url))
const ART = join(HERE, '..', '..', 'firmware', 'ui', 'art')
const OUT = join(HERE, 'out')
const read = (name: string): unknown => JSON.parse(readFileSync(join(HERE, 'source', name), 'utf8'))

const face = read('maus-face.json') as Face
const body = read('maus-body-cursor.json') as Body
const states = read('states.json') as StatesFile
const palette = read('palette.json') as Palette

export const PROFILES = [{ name: 's240', height: 240 }, { name: 's150', height: 150 }] as const
const BLINK = [1.0, 0.6, 0.25, 0.04]
const EXPR_COUNT = 18

function render(svg: string, height: number): Rgba {
  const png = new Resvg(svg, { fitTo: { mode: 'height', value: height }, background: 'rgba(0,0,0,0)' }).render().asPng()
  const decoded = PNG.sync.read(png)
  return { width: decoded.width, height: decoded.height, data: new Uint8Array(decoded.data) }
}

/** Every visible face pixel must sit where the body is fully opaque (no clip mask is used). */
function assertInsideBody(layer: Rgba, bodyImg: Rgba, what: string): void {
  for (let i = 0; i < layer.width * layer.height; i++) {
    if (layer.data[i * 4 + 3] !== 0 && bodyImg.data[i * 4 + 3] !== 255) {
      throw new Error(`${what}: pixel ${i % layer.width},${Math.floor(i / layer.width)} lies outside the body`)
    }
  }
}

/**
 * The body's tight bounds (the shared viewBox) come from the Android twin,
 * MausBodies.kt, and the outline from shared/mascot-bodies.ts. Render the
 * outline at 4 px per face unit on a generous canvas and check that the two
 * agree within 0.5 face units, so a drifting copy can never clip or shift
 * the body silently.
 */
function checkBodyBounds(b: Body): void {
  const [x0, y0, size, perUnit] = [-64, -64, 384, 4]
  const t = b.fit
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${x0} ${y0} ${size} ${size}">` +
    `<g transform="translate(${t.tx} ${t.ty}) scale(${t.scale})"><path fill="#000" d="${b.path}"/></g></svg>`
  const box = trim(render(svg, size * perUnit))
  const drawn = {
    left: x0 + box.x / perUnit,
    top: y0 + box.y / perUnit,
    right: x0 + (box.x + box.image.width) / perUnit,
    bottom: y0 + (box.y + box.image.height) / perUnit,
  }
  for (const side of ['left', 'top', 'right', 'bottom'] as const) {
    if (Math.abs(drawn[side] - b.bounds[side]) > 0.5) {
      throw new Error(`MausBodies.kt bounds disagree with shared/mascot-bodies.ts: ${side} is ${b.bounds[side]}, ` +
        `the path reaches ${drawn[side]}`)
    }
  }
}

interface Layer { img: LvImage; x: number; y: number }

function layer(name: string, svg: string, height: number, bodyImg: Rgba, cf: ColorFormat): Layer {
  const full = render(svg, height)
  assertInsideBody(full, bodyImg, name)
  const t = trim(full)
  return { img: toLvImage(name, t.image, cf), x: t.x, y: t.y }
}

function declare(images: LvImage[]): string {
  return images.map(i => `extern const lv_image_dsc_t ${i.name};`).join('\n') + '\n'
}

function buildProfile(name: string, height: number, exprs: number[]): number {
  const dir = join(ART, name)
  rmSync(dir, { recursive: true, force: true })
  mkdirSync(dir, { recursive: true })
  const preview = join(OUT, name)
  mkdirSync(preview, { recursive: true })

  const bodyRgba = render(bodySvg(body, palette), height)
  const bodyImg = toLvImage(`maus_${name}_body`, bodyRgba, 'RGB565A8')
  const eyeCf: ColorFormat = palette.eye_format
  const eyes = exprs.map(e => BLINK.map((open, step) =>
    layer(`maus_${name}_eye_${e}_${step}`, eyesSvg(face, body, face.expressions[e], open, palette.face), height, bodyRgba, eyeCf)))
  const mouths = exprs.map(e =>
    layer(`maus_${name}_mouth_${e}`, mouthSvg(face, body, face.expressions[e], palette.face), height, bodyRgba, 'RGB565A8'))
  const speak = states.speak.expressions.map(e => states.speak.open.map((open, i) =>
    layer(`maus_${name}_speak_${e}_${i + 1}`,
      speakMouthSvg(face, body, face.expressions[e], open, i + 1, states.speak.widen, palette.face, palette.speak_interior),
      height, bodyRgba, 'RGB565A8')))

  const all: LvImage[] = [bodyImg, ...eyes.flat().map(l => l.img), ...mouths.map(l => l.img), ...speak.flat().map(l => l.img)]
  const total = all.reduce((s, i) => s + i.data.length, 0)
  const include = '#include "lvgl.h"\n\n'
  writeFileSync(join(dir, 'body.c'), C_HEADER + include + cImage(bodyImg))
  writeFileSync(join(dir, 'eyes.c'), C_HEADER + include + eyes.flat().map(l => cImage(l.img)).join('\n'))
  writeFileSync(join(dir, 'mouths.c'), C_HEADER + include + mouths.map(l => cImage(l.img)).join('\n'))
  writeFileSync(join(dir, 'speak.c'), C_HEADER + include + speak.flat().map(l => cImage(l.img)).join('\n'))

  const idx = (e: number): number => {
    const i = exprs.indexOf(e)
    if (i < 0) throw new Error(`expression ${e} is not in the baked union`)
    return i
  }
  const lay = (l: Layer): string => `{&${l.img.name}, ${l.x}, ${l.y}}`
  const defs = stateDefs(states, face.face_box, height, body.bounds.bottom - body.bounds.top)
  const pools = defs.map(d => `static const uint8_t pool_${d.key}[] = {${d.pool.map(idx).join(', ')}};`).join('\n')
  const stateRows = defs.map(d =>
    `  [${d.enumName}] = {pool_${d.key}, ${d.pool.length}, ${d.cad[0]}, ${d.cad[1]}, ${d.blink[0]}, ${d.blink[1]}, ` +
    `${d.bob[0]}, ${d.bob[1]}, ${d.jitter[0]}, ${d.jitter[1]}, ${d.circle[0]}, ${d.circle[1]}},`).join('\n')
  const tables = C_HEADER + '#include "maus_art.h"\n\n' + declare(all) + '\n' +
    `static const uint8_t expr_ids[MAUS_EXPR_COUNT] = {${exprs.join(', ')}};\n\n` +
    `static const maus_layer_t eyes[MAUS_EXPR_COUNT][MAUS_BLINK_STEPS] = {\n` +
    eyes.map(row => `  {${row.map(lay).join(', ')}},`).join('\n') + '\n};\n\n' +
    `static const maus_layer_t mouth[MAUS_EXPR_COUNT] = {\n` + mouths.map(l => `  ${lay(l)},`).join('\n') + '\n};\n\n' +
    `static const uint8_t speak_expr[MAUS_SPEAK_EXPRS] = {${states.speak.expressions.map(idx).join(', ')}};\n\n` +
    `static const maus_layer_t speak[MAUS_SPEAK_EXPRS][MAUS_SPEAK_LEVELS] = {\n` +
    speak.map(row => `  {${row.map(lay).join(', ')}},`).join('\n') + '\n};\n\n' +
    pools + '\n\n' +
    `/* pool, pool_len, cadence ms, blink ms (0 = never), bob/jitter/circle: amplitude in 0.1 px, period ms */\n` +
    `static const maus_state_def_t states[UI_MAUS__COUNT] = {\n${stateRows}\n};\n\n` +
    `const maus_art_t maus_art_${name} = {\n` +
    `  .profile = "${name}",\n  .w = ${bodyImg.w},\n  .h = ${bodyImg.h},\n  .body = &${bodyImg.name},\n` +
    `  .expr_ids = expr_ids,\n  .eyes = eyes,\n  .mouth = mouth,\n  .speak_expr = speak_expr,\n  .speak = speak,\n` +
    `  .states = states,\n  .total_bytes = ${total},\n};\n`
  writeFileSync(join(dir, 'tables.c'), tables)

  writeFileSync(join(preview, 'body.png'), new Resvg(bodySvg(body, palette), { fitTo: { mode: 'height', value: height } }).render().asPng())
  for (const e of exprs) {
    writeFileSync(join(preview, `expr_${e}.png`),
      new Resvg(previewSvg(face, body, palette, face.expressions[e]), { fitTo: { mode: 'height', value: height } }).render().asPng())
  }
  console.log(`${name}: body ${bodyImg.w}x${bodyImg.h}, ${all.length} images, ${total} bytes`)
  return total
}

checkBodyBounds(body)
const exprs = unionOfPools(states)
if (exprs.length !== EXPR_COUNT) throw new Error(`state pools cover ${exprs.length} expressions; maus_art.h says ${EXPR_COUNT}`)
mkdirSync(ART, { recursive: true })
writeFileSync(join(ART, 'maus_art.h'), readFileSync(join(HERE, 'maus_art.h.in'), 'utf8'))
writeFileSync(join(ART, 'maus_art.c'), C_HEADER + `#include "maus_art.h"

/* MAUS_ART_HAS_S240 / MAUS_ART_HAS_S150 come from firmware/ui/CMakeLists.txt:
 * the desktop build links both profiles, an ESP32 build only its board's. */
const maus_art_t *maus_art_for(gadget_art_profile_t profile) {
  switch (profile) {
#if defined(MAUS_ART_HAS_S240)
  case GADGET_ART_S240:
    return &maus_art_s240;
#endif
#if defined(MAUS_ART_HAS_S150)
  case GADGET_ART_S150:
    return &maus_art_s150;
#endif
  default:
    return NULL;
  }
}
`)
for (const p of PROFILES) buildProfile(p.name, p.height, exprs)
