// SPDX-License-Identifier: Apache-2.0
// SVG builders for one Maus layer at a time. Every layer uses the same
// viewBox (the cursor body's tight bounds in face-box units), so layers
// rendered at the same height line up pixel for pixel with the body.

export type Point = [number, number]
export interface Expression {
  id: number
  eyes: Point[][]
  mouth: number[]            // [halfWidth, curve, gap, skew]
  mouth_frame: { x: number; y: number; angle: number }
}
export interface Face {
  face_box: number
  face_centre: [number, number]
  mouth_stroke: number
  expressions: Expression[]
}
export interface Body {
  fit: { tx: number; ty: number; scale: number }
  path: string
  anchor: { x: number; y: number; scale: number }
  bounds: { left: number; top: number; right: number; bottom: number }
}
export interface Palette {
  body_gradient: [string, number][]
  face: string
  speak_interior: string
  eye_format: 'A8' | 'RGB565A8'
}

const f = (v: number): string => (Math.round(v * 1000) / 1000).toString()

function wrap(body: Body, inner: string, defs = ''): string {
  const b = body.bounds
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${f(b.left)} ${f(b.top)} ${f(b.right - b.left)} ${f(b.bottom - b.top)}">` +
    `${defs ? `<defs>${defs}</defs>` : ''}${inner}</svg>`
}

function faceGroup(face: Face, body: Body, inner: string): string {
  const a = body.anchor
  const c = face.face_centre
  return `<g transform="translate(${f(a.x)} ${f(a.y)}) scale(${f(a.scale)}) translate(${f(-c[0])} ${f(-c[1])})">${inner}</g>`
}

/** The body alone, filled with the 3-stop gradient (corner to corner, top-right to bottom-left). */
export function bodySvg(body: Body, palette: Palette): string {
  const stops = palette.body_gradient.map(([color, at]) => `<stop offset="${f(at)}" stop-color="${color}"/>`).join('')
  const defs = `<linearGradient id="g" x1="1" y1="0" x2="0" y2="1">${stops}</linearGradient>`
  const t = body.fit
  return wrap(body, `<g transform="translate(${f(t.tx)} ${f(t.ty)}) scale(${f(t.scale)})"><path fill="url(#g)" d="${body.path}"/></g>`, defs)
}

/** Both eyes of one expression, squashed toward each eye's centre line by `open` (1 = open). */
export function eyesSvg(face: Face, body: Body, e: Expression, open: number, color: string): string {
  const paths = e.eyes.map(ring => {
    const cy = ring.reduce((s, p) => s + p[1], 0) / ring.length
    const d = ring.map((p, i) => `${i === 0 ? 'M' : 'L'}${f(p[0])} ${f(cy + (p[1] - cy) * open)}`).join(' ') + ' Z'
    return `<path fill="${color}" d="${d}"/>`
  })
  return wrap(body, faceGroup(face, body, paths.join('')))
}

function mouthPoint(e: Expression, lx: number, ly: number): string {
  const fr = e.mouth_frame
  const ca = Math.cos(fr.angle)
  const sa = Math.sin(fr.angle)
  return `${f(fr.x + lx * ca - ly * sa)} ${f(fr.y + lx * sa + ly * ca)}`
}

/** The expression's own closed mouth: one quadratic stroke with round caps. */
export function mouthSvg(face: Face, body: Body, e: Expression, color: string): string {
  const [hw, curve] = e.mouth
  const d = `M${mouthPoint(e, -hw, 0)} Q${mouthPoint(e, 0, curve)} ${mouthPoint(e, hw, 0)}`
  return wrap(body, faceGroup(face, body,
    `<path d="${d}" fill="none" stroke="${color}" stroke-width="${f(face.mouth_stroke)}" stroke-linecap="round"/>`))
}

/**
 * Gadget-only speaking mouth (original to this SDK): a closed lens between an
 * upper and a lower quadratic, `open` face units deep, widened by `widen` per level.
 */
export function speakMouthSvg(face: Face, body: Body, e: Expression, open: number, level: number, widen: number,
  stroke: string, interior: string): string {
  const hw = e.mouth[0] + widen * level
  const curve = e.mouth[1]
  const upper = Math.min(curve, 0) * 0.5 - open * 0.15
  const lower = curve + open
  const d = `M${mouthPoint(e, -hw, 0)} Q${mouthPoint(e, 0, upper)} ${mouthPoint(e, hw, 0)} ` +
    `Q${mouthPoint(e, 0, lower)} ${mouthPoint(e, -hw, 0)} Z`
  return wrap(body, faceGroup(face, body,
    `<path d="${d}" fill="${interior}" stroke="${stroke}" stroke-width="${f(face.mouth_stroke)}" stroke-linejoin="round"/>`))
}

/** Body plus face in one picture, for review previews only. */
export function previewSvg(face: Face, body: Body, palette: Palette, e: Expression): string {
  const bodyInner = bodySvg(body, palette).replace(/^<svg[^>]*>/, '').replace(/<\/svg>$/, '')
  const eyes = eyesSvg(face, body, e, 1, palette.face).replace(/^<svg[^>]*>/, '').replace(/<\/svg>$/, '')
  const mouth = mouthSvg(face, body, e, palette.face).replace(/^<svg[^>]*>/, '').replace(/<\/svg>$/, '')
  return wrap(body, bodyInner + eyes + mouth)
}
