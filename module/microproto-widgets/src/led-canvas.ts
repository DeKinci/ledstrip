import { WidgetFactory, PropertySchema, MicroProtoClient, Widget, TYPE } from './core/types'

/** Widget ID 1 for LIST<uint8_t> — LED preview canvas */
export const LedCanvas: WidgetFactory = {
  types: [TYPE.LIST],
  widgetId: 1,
  complex: true,

  create(schema: PropertySchema, _client: MicroProtoClient): Widget {
    const LED_R = 5
    const CANVAS_H = 200

    const el = document.createElement('div')
    el.style.cssText = 'background:#111;border-radius:16px;padding:8px;margin-bottom:16px'

    const canvas = document.createElement('canvas')
    canvas.height = CANVAS_H
    canvas.style.cssText = 'width:100%;display:block;border-radius:8px'
    el.appendChild(canvas)

    const ctx = canvas.getContext('2d')!
    let lastData: number[] | null = null

    function boostColor(r: number, g: number, b: number): [number, number, number] {
      const max = Math.max(r, g, b)
      if (max === 0) return [0, 0, 0]
      const boost = Math.max(1, 80 / max)
      return [Math.min(255, Math.round(r * boost)), Math.min(255, Math.round(g * boost)), Math.min(255, Math.round(b * boost))]
    }

    function draw() {
      if (!lastData || lastData.length === 0) return
      const dpr = window.devicePixelRatio || 1
      const w = canvas.clientWidth
      canvas.width = w * dpr
      canvas.height = CANVAS_H * dpr
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
      ctx.clearRect(0, 0, w, CANVAS_H)

      const ledCount = Math.floor(lastData.length / 3)
      const r = Math.min(7, Math.max(2, ((w - 20) / ledCount - 2) / 2))
      const sp = r * 2 + 2
      const totalW = ledCount * sp - 2
      const sx = (w - totalW) / 2

      for (let i = 0; i < ledCount; i++) {
        const off = i * 3
        const [dr, dg, db] = boostColor(lastData[off], lastData[off + 1], lastData[off + 2])
        ctx.beginPath()
        ctx.arc(sx + i * sp + r, CANVAS_H / 2, r, 0, Math.PI * 2)
        ctx.fillStyle = `rgb(${dr},${dg},${db})`
        ctx.fill()
      }
    }

    return {
      element: el,
      update(value: any) {
        lastData = value
        draw()
      }
    }
  }
}
