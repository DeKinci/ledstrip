import { WidgetFactory, PropertySchema, MicroProtoClient, Widget, TYPE } from './core/types'

const SEG_NAME_LEN = 8
const SEG_TYPES = ['line', 'ring', 'matrix']

interface Segment {
  name: string; type: string; startIndex: number; ledCount: number
  width: number; height: number; serpentine: number; reverse: number
  x: number; y: number; rotation: number
}

/** Widget ID 1 for LIST<OBJECT> — segment layout editor */
export const SegmentEditor: WidgetFactory = {
  types: [TYPE.LIST],
  widgetId: 1,
  complex: true,

  create(schema: PropertySchema, client: MicroProtoClient): Widget {
    let segments: Segment[] = []
    let selectedIdx = -1
    let ledColors: number[] | null = null

    function getColor(gi: number): [number, number, number] {
      if (!ledColors) return [50, 50, 50]
      const off = gi * 3
      if (off + 2 >= ledColors.length) return [30, 30, 30]
      const r = ledColors[off], g = ledColors[off + 1], b = ledColors[off + 2]
      const max = Math.max(r, g, b)
      if (max === 0) return [0, 0, 0]
      const boost = Math.max(1, 80 / max)
      return [Math.min(255, Math.round(r * boost)), Math.min(255, Math.round(g * boost)), Math.min(255, Math.round(b * boost))]
    }

    const el = document.createElement('div')

    // Toolbar
    const toolbar = document.createElement('div')
    toolbar.style.cssText = 'display:flex;gap:6px;margin-bottom:6px;align-items:center'
    toolbar.innerHTML = `
      <button class="mp-btn mp-btn-sm mp-btn-teal" id="addSeg">+ Segment</button>
      <button class="mp-btn mp-btn-sm mp-btn-danger" id="delSeg" style="display:none">Delete</button>
      <button class="mp-btn mp-btn-sm mp-btn-primary" id="saveSeg">Save</button>
      <button class="mp-btn mp-btn-sm mp-btn-ghost" id="copySeg">Copy</button>
      <button class="mp-btn mp-btn-sm mp-btn-ghost" id="pasteSeg">Paste</button>
    `
    el.appendChild(toolbar)

    // Canvas
    const canvas = document.createElement('canvas')
    canvas.height = 200
    canvas.style.cssText = 'width:100%;display:block;border-radius:8px;background:#111;cursor:default'
    el.appendChild(canvas)
    const ctx = canvas.getContext('2d')!

    // Properties panel
    const propsDiv = document.createElement('div')
    propsDiv.style.cssText = 'display:none;background:#1a1a1a;border-radius:12px;padding:12px 16px;margin-top:8px;border-left:3px solid #14b8a6'
    propsDiv.innerHTML = `
      <div style="font-size:13px;font-weight:600;margin-bottom:8px;display:flex;justify-content:space-between">
        <span id="segTitle">Segment</span>
        <div style="display:flex;gap:4px;align-items:center">
          <label style="font-size:11px;color:#888">Order</label>
          <input type="number" id="segOrder" min="0" style="width:48px;padding:4px 6px;background:#252525;border:1px solid #333;border-radius:4px;color:white;font-size:12px">
        </div>
      </div>
      <div style="display:flex;gap:8px;align-items:center;margin-bottom:8px">
        <label style="font-size:11px;color:#888;min-width:36px">Name</label>
        <input type="text" id="segName" maxlength="7" style="flex:1;padding:5px 8px;background:#252525;border:1px solid #333;border-radius:4px;color:white;font-size:12px">
        <label style="font-size:11px;color:#888;min-width:36px">Type</label>
        <select id="segType" style="padding:5px 8px;background:#252525;border:1px solid #333;border-radius:4px;color:white;font-size:12px">
          <option value="line">Line</option><option value="ring">Ring</option><option value="matrix">Matrix</option>
        </select>
        <label style="font-size:11px;color:#888;min-width:36px">LEDs</label>
        <input type="number" id="segLeds" min="1" max="1000" style="width:60px;padding:5px 8px;background:#252525;border:1px solid #333;border-radius:4px;color:white;font-size:12px">
        <label style="min-width:auto;font-size:11px;color:#888"><input type="checkbox" id="segReverse"> Rev</label>
      </div>
      <div id="matrixRow" style="display:none;display:flex;gap:8px;align-items:center">
        <label style="font-size:11px;color:#888;min-width:36px">W</label>
        <input type="number" id="segW" min="1" style="width:60px;padding:5px 8px;background:#252525;border:1px solid #333;border-radius:4px;color:white;font-size:12px">
        <label style="font-size:11px;color:#888;min-width:36px">H</label>
        <input type="number" id="segH" min="1" style="width:60px;padding:5px 8px;background:#252525;border:1px solid #333;border-radius:4px;color:white;font-size:12px">
        <label style="min-width:auto;font-size:11px;color:#888"><input type="checkbox" id="segSerp"> Serpentine</label>
      </div>
    `
    el.appendChild(propsDiv)

    // Decode segments from MicroProto LIST(OBJECT)
    function decodeSegments(value: any[]): Segment[] {
      let startIdx = 0
      return value.map(obj => {
        const nameArr = obj.name || []
        const name = String.fromCharCode(...(Array.isArray(nameArr) ? nameArr : []).filter((b: number) => b))
        const flags = obj.flags || 0
        const seg: Segment = {
          name, type: SEG_TYPES[flags & 0x03] || 'line',
          startIndex: startIdx, ledCount: obj.ledCount || 0,
          width: obj.width || 0, height: obj.height || 0,
          serpentine: (flags & 0x04) ? 1 : 0, reverse: (flags & 0x08) ? 1 : 0,
          x: obj.x || 0, y: obj.y || 0, rotation: obj.rotation || 0,
        }
        startIdx += seg.ledCount
        return seg
      })
    }

    function encodeSegments(segs: Segment[]) {
      return segs.map(s => {
        const nameBytes = new Array(SEG_NAME_LEN).fill(0)
        const enc = new TextEncoder().encode(s.name.substring(0, SEG_NAME_LEN - 1))
        for (let i = 0; i < enc.length; i++) nameBytes[i] = enc[i]
        return {
          name: nameBytes, ledCount: s.ledCount,
          x: s.x || 0, y: s.y || 0, rotation: s.rotation || 0,
          flags: (SEG_TYPES.indexOf(s.type) & 0x03) | (s.serpentine ? 0x04 : 0) | (s.reverse ? 0x08 : 0),
          width: s.type === 'matrix' ? (s.width || 1) : 0,
          height: s.type === 'matrix' ? (s.height || 1) : 0,
          _reserved: 0,
        }
      })
    }

    function save() { client.setProperty(schema.name, encodeSegments(segments)) }

    function select(idx: number) {
      selectedIdx = idx
      const del = toolbar.querySelector('#delSeg') as HTMLElement
      del.style.display = idx >= 0 ? '' : 'none'
      if (idx < 0 || idx >= segments.length) {
        propsDiv.style.display = 'none'
        return
      }
      propsDiv.style.display = 'block'
      const seg = segments[idx]
      ;(propsDiv.querySelector('#segTitle') as HTMLElement).textContent = '#' + idx + ' ' + seg.name
      ;(propsDiv.querySelector('#segOrder') as HTMLInputElement).value = String(idx)
      ;(propsDiv.querySelector('#segName') as HTMLInputElement).value = seg.name
      ;(propsDiv.querySelector('#segType') as HTMLSelectElement).value = seg.type
      ;(propsDiv.querySelector('#segLeds') as HTMLInputElement).value = String(seg.ledCount)
      ;(propsDiv.querySelector('#segLeds') as HTMLInputElement).disabled = seg.type === 'matrix'
      ;(propsDiv.querySelector('#segReverse') as HTMLInputElement).checked = !!seg.reverse
      const mrow = propsDiv.querySelector('#matrixRow') as HTMLElement
      mrow.style.display = seg.type === 'matrix' ? 'flex' : 'none'
      if (seg.type === 'matrix') {
        ;(propsDiv.querySelector('#segW') as HTMLInputElement).value = String(seg.width || 1)
        ;(propsDiv.querySelector('#segH') as HTMLInputElement).value = String(seg.height || 1)
        ;(propsDiv.querySelector('#segSerp') as HTMLInputElement).checked = !!seg.serpentine
      }
      drawCanvas()
    }

    // Bind property inputs
    for (const id of ['segName', 'segType', 'segLeds', 'segW', 'segH', 'segSerp', 'segReverse']) {
      propsDiv.querySelector('#' + id)?.addEventListener('change', () => {
        if (selectedIdx < 0) return
        const seg = segments[selectedIdx]
        seg.name = (propsDiv.querySelector('#segName') as HTMLInputElement).value
        seg.type = (propsDiv.querySelector('#segType') as HTMLSelectElement).value
        if (seg.type === 'matrix') {
          seg.width = parseInt((propsDiv.querySelector('#segW') as HTMLInputElement).value) || 1
          seg.height = parseInt((propsDiv.querySelector('#segH') as HTMLInputElement).value) || 1
          seg.ledCount = seg.width * seg.height
          seg.serpentine = (propsDiv.querySelector('#segSerp') as HTMLInputElement).checked ? 1 : 0
        } else {
          seg.ledCount = parseInt((propsDiv.querySelector('#segLeds') as HTMLInputElement).value) || 1
        }
        seg.reverse = (propsDiv.querySelector('#segReverse') as HTMLInputElement).checked ? 1 : 0
        select(selectedIdx)
      })
    }

    propsDiv.querySelector('#segOrder')?.addEventListener('change', () => {
      if (selectedIdx < 0) return
      const newIdx = Math.max(0, Math.min(segments.length - 1, parseInt((propsDiv.querySelector('#segOrder') as HTMLInputElement).value) || 0))
      if (newIdx === selectedIdx) return
      const [seg] = segments.splice(selectedIdx, 1)
      segments.splice(newIdx, 0, seg)
      select(newIdx)
    })

    // Toolbar buttons
    toolbar.querySelector('#addSeg')!.addEventListener('click', () => {
      segments.push({ name: 'seg' + segments.length, type: 'line', startIndex: 0, ledCount: 10, width: 0, height: 0, serpentine: 0, reverse: 0, x: 0, y: 0, rotation: 0 })
      select(segments.length - 1)
    })
    toolbar.querySelector('#delSeg')!.addEventListener('click', () => {
      if (selectedIdx >= 0) { segments.splice(selectedIdx, 1); select(-1) }
    })
    toolbar.querySelector('#saveSeg')!.addEventListener('click', save)
    toolbar.querySelector('#copySeg')!.addEventListener('click', () => {
      const ta = document.createElement('textarea')
      ta.value = JSON.stringify(segments, null, 2)
      ta.style.cssText = 'position:fixed;opacity:0'
      document.body.appendChild(ta); ta.select(); document.execCommand('copy'); document.body.removeChild(ta)
    })
    toolbar.querySelector('#pasteSeg')!.addEventListener('click', () => {
      const text = prompt('Paste segment JSON:')
      if (!text) return
      try {
        segments = JSON.parse(text)
        select(-1)
      } catch (e) { alert('Invalid JSON') }
    })

    // Canvas interaction
    let dragState: any = null
    const LED_R = 5, HANDLE_R = 6, HANDLE_DIST = 20, CANVAS_H = 200

    function worldToCanvas(wx: number, wy: number) {
      const w = canvas.clientWidth
      const scale = Math.min(w / 400, CANVAS_H / 200)
      const cx = w / 2, cy = CANVAS_H / 2
      return { x: cx + wx * scale, y: cy + wy * scale }
    }

    function getSegBounds(seg: Segment) {
      const sp = LED_R * 2 + 2, pad = LED_R + 4
      if (seg.type === 'ring') {
        const radius = Math.max(10, seg.ledCount * sp / (2 * Math.PI)) + pad
        return { w: radius, h: radius, radius, isRing: true }
      }
      if (seg.type === 'matrix') {
        return { w: (seg.width || 1) * sp / 2 + pad, h: (seg.height || 1) * sp / 2 + pad, radius: 0, isRing: false }
      }
      return { w: seg.ledCount * sp / 2 + pad, h: LED_R + pad, radius: 0, isRing: false }
    }

    function hitTest(mx: number, my: number) {
      for (let i = segments.length - 1; i >= 0; i--) {
        const seg = segments[i], c = worldToCanvas(seg.x, seg.y)
        const b = getSegBounds(seg), rot = seg.rotation * Math.PI / 180
        const ha = rot - Math.PI / 2
        const hd = (b.isRing ? b.radius : b.h) + HANDLE_DIST
        if (Math.hypot(mx - (c.x + Math.cos(ha) * hd), my - (c.y + Math.sin(ha) * hd)) < HANDLE_R + 4)
          return { idx: i, part: 'rotate' as const }
        if (b.isRing) {
          const dist = Math.hypot(mx - c.x, my - c.y)
          const ringR = Math.max(10, seg.ledCount * (LED_R * 2 + 2) / (2 * Math.PI))
          if (dist > ringR - LED_R - 4 && dist < ringR + LED_R + 4) return { idx: i, part: 'body' as const }
        } else {
          const dx = mx - c.x, dy = my - c.y
          const lx = dx * Math.cos(-rot) - dy * Math.sin(-rot)
          const ly = dx * Math.sin(-rot) + dy * Math.cos(-rot)
          if (Math.abs(lx) < b.w && Math.abs(ly) < b.h) return { idx: i, part: 'body' as const }
        }
      }
      return null
    }

    canvas.addEventListener('pointerdown', e => {
      const rect = canvas.getBoundingClientRect()
      const mx = e.clientX - rect.left, my = e.clientY - rect.top
      const hit = hitTest(mx, my)
      if (!hit) { select(-1); return }
      select(hit.idx)
      const seg = segments[hit.idx], c = worldToCanvas(seg.x, seg.y)
      if (hit.part === 'rotate') {
        dragState = { type: 'rotate', idx: hit.idx, cx: c.x, cy: c.y, origRot: seg.rotation, startAngle: Math.atan2(my - c.y, mx - c.x) }
      } else {
        dragState = { type: 'move', idx: hit.idx, startX: mx, startY: my, origX: seg.x, origY: seg.y }
      }
      canvas.setPointerCapture(e.pointerId)
    })

    canvas.addEventListener('pointermove', e => {
      const rect = canvas.getBoundingClientRect()
      const mx = e.clientX - rect.left, my = e.clientY - rect.top
      if (!dragState) { const h = hitTest(mx, my); canvas.style.cursor = h ? (h.part === 'rotate' ? 'crosshair' : 'grab') : 'default'; return }
      const seg = segments[dragState.idx]
      if (dragState.type === 'move') {
        const w = canvas.clientWidth
        const scale = Math.min(w / 400, CANVAS_H / 200)
        seg.x = Math.round(Math.max(-200, Math.min(200, dragState.origX + (mx - dragState.startX) / scale)))
        seg.y = Math.round(Math.max(-100, Math.min(100, dragState.origY + (my - dragState.startY) / scale)))
        canvas.style.cursor = 'grabbing'
      } else {
        let deg = dragState.origRot + (Math.atan2(my - dragState.cy, mx - dragState.cx) - dragState.startAngle) * 180 / Math.PI
        const snapped = Math.round(deg / 15) * 15
        if (Math.abs(deg - snapped) < 3) deg = snapped
        seg.rotation = Math.round(deg)
      }
      select(selectedIdx)
    })

    canvas.addEventListener('pointerup', () => { dragState = null })

    function recalcStartIndices() {
      let idx = 0; segments.forEach(s => { s.startIndex = idx; idx += s.ledCount })
    }

    function drawCanvas() {
      recalcStartIndices()
      const dpr = window.devicePixelRatio || 1, w = canvas.clientWidth
      canvas.width = w * dpr; canvas.height = CANVAS_H * dpr
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
      ctx.clearRect(0, 0, w, CANVAS_H)

      // Flat strip fallback when no segments defined
      if (segments.length === 0 && ledColors && ledColors.length > 0) {
        const ledCount = Math.floor(ledColors.length / 3)
        const r = Math.min(7, Math.max(2, ((w - 20) / ledCount - 2) / 2))
        const sp = r * 2 + 2
        const totalW = ledCount * sp - 2
        const sx = (w - totalW) / 2
        for (let i = 0; i < ledCount; i++) {
          const [cr, cg, cb] = getColor(i)
          ctx.beginPath(); ctx.arc(sx + i * sp + r, CANVAS_H / 2, r, 0, Math.PI * 2)
          ctx.fillStyle = `rgb(${cr},${cg},${cb})`; ctx.fill()
        }
        return
      }
      if (segments.length === 0) return

      segments.forEach((seg, idx) => {
        const c = worldToCanvas(seg.x, seg.y), rot = seg.rotation * Math.PI / 180
        const selected = idx === selectedIdx, b = getSegBounds(seg)
        ctx.save(); ctx.translate(c.x, c.y); ctx.rotate(rot)
        if (selected) {
          ctx.strokeStyle = '#14b8a6'; ctx.lineWidth = 1.5; ctx.setLineDash([4, 3])
          if (b.isRing) { ctx.beginPath(); ctx.arc(0, 0, b.radius, 0, Math.PI * 2); ctx.stroke() }
          else ctx.strokeRect(-b.w, -b.h, b.w * 2, b.h * 2)
          ctx.setLineDash([])
        }
        const sp = LED_R * 2 + 2
        function ledIdx(li: number, lx: number, ly: number) {
          if (!selected) return
          ctx.fillStyle = '#fff'; ctx.font = '7px sans-serif'
          ctx.textAlign = 'center'; ctx.textBaseline = 'middle'
          ctx.fillText(String(li), lx, ly)
        }
        if (seg.type === 'ring') {
          const n = seg.ledCount, radius = Math.max(10, n * sp / (2 * Math.PI))
          for (let i = 0; i < n; i++) {
            const a = (i / n) * Math.PI * 2 - Math.PI / 2
            const [cr, cg, cb] = getColor(seg.startIndex + i)
            ctx.beginPath(); ctx.arc(Math.cos(a) * radius, Math.sin(a) * radius, LED_R, 0, Math.PI * 2)
            ctx.fillStyle = `rgb(${cr},${cg},${cb})`; ctx.fill()
            ledIdx(i + 1, Math.cos(a) * radius, Math.sin(a) * radius)
          }
        } else if (seg.type === 'matrix') {
          const cols = seg.width || 1, rows = seg.height || 1
          const sx = -(cols * sp - 2) / 2, sy = -(rows * sp - 2) / 2
          for (let r = 0; r < rows; r++) for (let cc = 0; cc < cols; cc++) {
            const mi = (seg.serpentine && (r & 1)) ? r * cols + (cols - 1 - cc) : r * cols + cc
            const [cr, cg, cb] = getColor(seg.startIndex + mi)
            const px = sx + cc * sp + LED_R, py = sy + r * sp + LED_R
            ctx.beginPath(); ctx.arc(px, py, LED_R, 0, Math.PI * 2)
            ctx.fillStyle = `rgb(${cr},${cg},${cb})`; ctx.fill()
            ledIdx(mi + 1, px, py)
          }
        } else {
          const n = seg.ledCount, sx = -(n * sp - 2) / 2
          for (let i = 0; i < n; i++) {
            const [cr, cg, cb] = getColor(seg.startIndex + i)
            const px = sx + i * sp + LED_R
            ctx.beginPath(); ctx.arc(px, 0, LED_R, 0, Math.PI * 2)
            ctx.fillStyle = `rgb(${cr},${cg},${cb})`; ctx.fill()
            ledIdx(i + 1, px, 0)
          }
        }
        ctx.restore()
        // Handle + label
        if (selected) {
          const ha = rot - Math.PI / 2, hd = (b.isRing ? b.radius : b.h) + HANDLE_DIST
          const hx = c.x + Math.cos(ha) * hd, hy = c.y + Math.sin(ha) * hd
          ctx.strokeStyle = '#14b8a6'; ctx.lineWidth = 1
          ctx.beginPath(); ctx.moveTo(c.x + Math.cos(ha) * (b.isRing ? b.radius : b.h), c.y + Math.sin(ha) * (b.isRing ? b.radius : b.h))
          ctx.lineTo(hx, hy); ctx.stroke()
          ctx.beginPath(); ctx.arc(hx, hy, HANDLE_R, 0, Math.PI * 2); ctx.fillStyle = '#14b8a6'; ctx.fill()
          ctx.strokeStyle = '#fff'; ctx.lineWidth = 1.5; ctx.stroke()
        }
        const labelY = b.isRing ? b.radius : b.h
        ctx.fillStyle = selected ? '#14b8a6' : '#555'
        ctx.font = (selected ? 'bold ' : '') + '10px sans-serif'; ctx.textAlign = 'center'
        ctx.fillText(`#${idx} ${seg.name} [${seg.startIndex}..${seg.startIndex + seg.ledCount - 1}]`, c.x, c.y + labelY + 12)
      })
    }

    // Subscribe to ledPreview for live colors
    client.on('property', (_id: number, name: string, value: any) => {
      if (name === 'ledPreview') {
        ledColors = value
        drawCanvas()
      }
    })

    return {
      element: el,
      update(value: any) {
        if (Array.isArray(value)) {
          segments = decodeSegments(value)
          select(-1)
          drawCanvas()
        }
      },
      destroy() { dragState = null }
    }
  }
}
