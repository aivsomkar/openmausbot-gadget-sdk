// SPDX-License-Identifier: Apache-2.0
import assert from 'node:assert/strict'
import { test } from 'node:test'
import { cImage, rgb565, toLvImage, trim, type Rgba } from '../lvgl.ts'

function rgba(width: number, height: number, px: (x: number, y: number) => [number, number, number, number]): Rgba {
  const data = new Uint8Array(width * height * 4)
  for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) data.set(px(x, y), (y * width + x) * 4)
  return { width, height, data }
}

test('rgb565 keeps the extremes exact under the dither', () => {
  for (let y = 0; y < 4; y++) {
    for (let x = 0; x < 4; x++) {
      assert.equal(rgb565(255, 255, 255, x, y), 0xffff)
      assert.equal(rgb565(0, 0, 0, x, y), 0x0000)
    }
  }
  assert.equal(rgb565(0, 153, 87, 0, 0), (0 << 11) | (38 << 5) | 10) // #009957, Bayer threshold 0
})

test('trim crops to alpha > 0 and reports the offset', () => {
  const img = rgba(10, 8, (x, y) => (x >= 3 && x <= 5 && y >= 2 && y <= 6 ? [255, 255, 255, 255] : [0, 0, 0, 0]))
  const t = trim(img)
  assert.deepEqual([t.x, t.y, t.image.width, t.image.height], [3, 2, 3, 5])
  assert.throws(() => trim(rgba(2, 2, () => [0, 0, 0, 0])), /fully transparent/)
})

test('RGB565A8 is the colour plane then the alpha plane', () => {
  const img = rgba(2, 1, x => (x === 0 ? [255, 255, 255, 255] : [255, 0, 0, 128]))
  const lv = toLvImage('t', img, 'RGB565A8')
  assert.equal(lv.stride, 4)
  assert.deepEqual(Array.from(lv.data), [0xff, 0xff, 0x00, 0xf8, 255, 128])
})

test('A8 keeps only alpha, and transparent pixels carry no colour', () => {
  const img = rgba(3, 1, x => [200, 10, 10, x * 100])
  assert.deepEqual(Array.from(toLvImage('e', img, 'A8').data), [0, 100, 200])
  assert.deepEqual(Array.from(toLvImage('m', img, 'RGB565A8').data.subarray(0, 2)), [0, 0])
})

test('C output is an LVGL 9 image descriptor', () => {
  const c = cImage(toLvImage('maus_s150_eye_6_0', rgba(2, 2, () => [255, 255, 255, 255]), 'A8'))
  assert.match(c, /static const LV_ATTRIBUTE_MEM_ALIGN uint8_t maus_s150_eye_6_0_map\[\] = \{\n  0xff,0xff,0xff,0xff\n\};/)
  assert.match(c, /\.cf = LV_COLOR_FORMAT_A8,/)
  assert.match(c, /\.w = 2,\n    \.h = 2,\n    \.stride = 2,/)
  assert.match(c, /\.data_size = sizeof\(maus_s150_eye_6_0_map\),/)
})
