import { WidgetFactory, PropertySchema, MicroProtoClient, Widget, TYPE } from './core/types'

declare const ace: any

/** Widget ID 2 for RESOURCE type — code editor with Ace */
export const CodeEditor: WidgetFactory = {
  types: [TYPE.RESOURCE],
  widgetId: 2,
  complex: true,

  create(schema: PropertySchema, client: MicroProtoClient): Widget {
    const el = document.createElement('div')
    el.className = 'mp-code-editor'

    // Shader list
    const listDiv = document.createElement('div')
    listDiv.style.cssText = 'display:flex;flex-direction:column;gap:8px;margin-bottom:16px'
    el.appendChild(listDiv)

    // Editor header
    const header = document.createElement('div')
    header.style.cssText = 'display:flex;align-items:center;gap:12px;margin-bottom:12px'
    header.innerHTML = `
      <input type="text" id="shaderName" placeholder="shader_name" style="flex:1;padding:10px 14px;background:#1a1a1a;border:1px solid #333;border-radius:8px;color:white;font-size:14px">
      <button class="mp-btn mp-btn-primary" id="saveBtn">Save</button>
      <button class="mp-btn mp-btn-ghost" id="newBtn">New</button>
    `
    el.appendChild(header)

    // Editor container with info button
    const editorWrap = document.createElement('div')
    editorWrap.style.cssText = 'position:relative'

    const infoBtn = document.createElement('button')
    infoBtn.textContent = 'i'
    infoBtn.style.cssText = 'position:absolute;top:6px;right:20px;z-index:10;background:rgba(255,255,255,0.08);border:none;color:#666;font-size:13px;width:22px;height:22px;border-radius:50%;cursor:pointer;line-height:22px;text-align:center'
    infoBtn.title = 'API reference'
    editorWrap.appendChild(infoBtn)

    const infoPanel = document.createElement('div')
    infoPanel.style.cssText = 'display:none;position:absolute;top:32px;right:8px;z-index:10;background:#1a1a1a;border:1px solid #333;border-radius:8px;padding:12px;font-size:11px;color:#aaa;max-width:320px;white-space:pre-line;line-height:1.6'
    infoPanel.innerHTML = `
<b style="color:#e5e5e5">Globals</b>
<span style="color:#14b8a6">led</span> — all LEDs segment
<span style="color:#14b8a6">e</span>.time .dt .frame .lum .clr .spd .volume

<b style="color:#e5e5e5">Colors</b>
<span style="color:#f59e0b">hsv</span>(h, s, v)  <span style="color:#f59e0b">rgb</span>(r, g, b)  <span style="color:#f59e0b">blend</span>(c1, c2, t)
0xFF0000 — hex color literal

<b style="color:#e5e5e5">Random</b>
<span style="color:#f59e0b">rand</span>(min, max [, period])
<span style="color:#f59e0b">jitter</span>(val, range [, period])

<b style="color:#e5e5e5">Segment</b>
seg[i]  #seg  seg.count  seg.width  seg.height
seg:blur(n) :fade(f) :shift(n) :mirror() :fill(c)
seg:set(col, row, color) — matrix
for i in seg do ... end — 1-based iterator

<b style="color:#e5e5e5">Animation</b>
<span style="color:#f59e0b">over</span>(secs, fn)  <span style="color:#f59e0b">wait</span>(secs)  <span style="color:#f59e0b">lerp</span>(a, b, t)
<span style="color:#f59e0b">frame</span>([secs]) — yield one frame, or hold for secs`
    editorWrap.appendChild(infoPanel)

    infoBtn.onclick = () => {
      infoPanel.style.display = infoPanel.style.display === 'none' ? 'block' : 'none'
    }

    const editorDiv = document.createElement('div')
    editorDiv.id = 'mp-editor-' + schema.name
    editorDiv.style.cssText = 'height:300px;border-radius:8px;font-size:13px'
    editorDiv.textContent = '-- New shader\nfunction draw()\n    for i in led do\n        led[i] = hsv(e.clr + e.time * 100 * e.spd + i * 5, 1, e.lum)\n    end\nend'
    editorWrap.appendChild(editorDiv)
    el.appendChild(editorWrap)

    // Error bar
    const errorBar = document.createElement('div')
    errorBar.style.cssText = 'display:none;padding:8px 12px;background:#dc2626;color:white;font-size:12px;border-radius:0 0 8px 8px;font-family:monospace;white-space:pre-wrap'
    el.appendChild(errorBar)

    // State
    let shaders: Array<{ id: number; name: string }> = []
    let currentIndex = 0
    let aceEditor: any = null

    // Init Ace (deferred until element is in DOM)
    setTimeout(() => {
      if (typeof ace === 'undefined') return
      aceEditor = ace.edit(editorDiv)
      aceEditor.setTheme('ace/theme/monokai')
      aceEditor.session.setMode('ace/mode/lua')
      aceEditor.setOptions({ fontSize: '13px', enableBasicAutocompletion: true, enableLiveAutocompletion: true })
      aceEditor.completers = [{ getCompletions: (_e: any, _s: any, _p: any, _pr: any, cb: any) => cb(null, completions()) }]
      aceEditor.commands.addCommand({
        name: 'save', bindKey: { win: 'Ctrl-S', mac: 'Cmd-S' },
        exec: () => saveShader()
      })
    }, 0)

    function completions() {
      const c = [
        { value: 'e.time', score: 100, meta: 'float', caption: 'e.time', docText: 'Time in seconds' },
        { value: 'e.dt', score: 99, meta: 'float', caption: 'e.dt', docText: 'Delta time' },
        { value: 'e.frame', score: 98, meta: 'int', caption: 'e.frame', docText: 'Frame count' },
        { value: 'e.lum', score: 97, meta: 'float', caption: 'e.lum', docText: 'Brightness 0-1' },
        { value: 'e.clr', score: 96, meta: 'int', caption: 'e.clr', docText: 'Color 0-255' },
        { value: 'e.spd', score: 95, meta: 'float', caption: 'e.spd', docText: 'Speed 0-2' },
        { value: 'rgb(r, g, b)', score: 90, meta: 'color', caption: 'rgb' },
        { value: 'hsv(h, s, v)', score: 89, meta: 'color', caption: 'hsv' },
        { value: 'rand(min, max)', score: 88, meta: 'func', caption: 'rand' },
        { value: 'jitter(val, range)', score: 87, meta: 'func', caption: 'jitter' },
        { value: 'blend(c1, c2, f)', score: 86, meta: 'color', caption: 'blend' },
        { value: 'lerp(a, b, t)', score: 85, meta: 'func', caption: 'lerp' },
        { value: 'over(secs, fn)', score: 84, meta: 'func', caption: 'over' },
        { value: 'wait(secs)', score: 83, meta: 'func', caption: 'wait' },
        { value: 'frame()', score: 82, meta: 'func', caption: 'frame' },
        { value: 'draw()', score: 81, meta: 'func', caption: 'draw' },
        { value: 'led', score: 80, meta: 'seg', caption: 'led' },
      ]
      return c
    }

    function decodeHeader(h: any): string {
      if (typeof h === 'string') return h
      if (Array.isArray(h)) return String.fromCharCode(...h.filter((b: number) => b))
      return String(h || '')
    }

    function renderList() {
      listDiv.innerHTML = ''
      shaders.forEach((sh, idx) => {
        const item = document.createElement('div')
        item.style.cssText = 'display:flex;align-items:center;gap:12px;padding:12px 16px;background:#1a1a1a;border-radius:8px;cursor:pointer;transition:background 0.15s'
        if (idx === currentIndex) item.style.cssText += ';background:#1e3a5f;border:1px solid #3b82f6'
        item.innerHTML = `
          <div style="width:20px;height:20px;border:2px solid ${idx === currentIndex ? '#3b82f6' : '#444'};border-radius:50%;display:flex;align-items:center;justify-content:center;font-size:12px;${idx === currentIndex ? 'background:#3b82f6' : ''}">${idx === currentIndex ? '\u2713' : ''}</div>
          <span style="flex:1;font-weight:500">${sh.name}</span>
          <button class="mp-btn mp-btn-sm mp-btn-warning" data-action="edit" data-id="${sh.id}" data-name="${sh.name}">Edit</button>
          <button class="mp-btn mp-btn-sm mp-btn-danger" data-action="del" data-id="${sh.id}" data-name="${sh.name}">Del</button>
        `
        item.onclick = (ev) => {
          const target = ev.target as HTMLElement
          if (target.dataset.action === 'edit') {
            editShader(Number(target.dataset.id), target.dataset.name || '')
          } else if (target.dataset.action === 'del') {
            deleteShader(Number(target.dataset.id), target.dataset.name || '')
          } else {
            client.setProperty('shaderIndex', idx)
          }
        }
        listDiv.appendChild(item)
      })
    }

    async function editShader(id: number, name: string) {
      const nameInput = header.querySelector('#shaderName') as HTMLInputElement
      nameInput.value = name
      nameInput.dataset.resourceId = String(id)
      try {
        const result = await client.getResource(schema.name, id)
        if (result?.data && aceEditor) {
          aceEditor.setValue(new TextDecoder().decode(result.data), -1)
        }
      } catch (e) { console.error('Load failed:', e) }
    }

    async function deleteShader(id: number, name: string) {
      if (!confirm(`Delete "${name}"?`)) return
      try { await client.deleteResource(schema.name, id) } catch (e) { console.error(e) }
    }

    async function saveShader() {
      const nameInput = header.querySelector('#shaderName') as HTMLInputElement
      const name = nameInput.value.trim()
      if (!name || !aceEditor) return
      try {
        const existingId = parseInt(nameInput.dataset.resourceId || '0') || 0
        const result = await client.putResource(schema.name, existingId, {
          header: new TextEncoder().encode(name),
          body: new TextEncoder().encode(aceEditor.getValue())
        })
        if (result?.resourceId) nameInput.dataset.resourceId = String(result.resourceId)
      } catch (e) { console.error(e) }
    }

    function newShader() {
      const nameInput = header.querySelector('#shaderName') as HTMLInputElement
      nameInput.value = ''
      nameInput.dataset.resourceId = '0'
      if (aceEditor) {
        aceEditor.setValue("-- New shader\nfunction draw()\n    for i in led do\n        led[i] = hsv(e.clr + e.time * 100 * e.spd + i * 5, 1, e.lum)\n    end\nend", -1)
      }
    }

    ;(header.querySelector('#saveBtn') as HTMLElement).onclick = saveShader
    ;(header.querySelector('#newBtn') as HTMLElement).onclick = newShader

    // Subscribe to related properties
    client.on('property', (_id: number, name: string, value: any) => {
      if (name === 'shaderIndex') {
        currentIndex = value
        renderList()
      } else if (name === 'shaderError') {
        const err = Array.isArray(value)
          ? String.fromCharCode(...value.filter((b: number) => b))
          : String(value || '')
        if (err) {
          errorBar.textContent = err
          errorBar.style.display = 'block'
        } else {
          errorBar.style.display = 'none'
        }
      }
    })

    return {
      element: el,
      update(value: any) {
        if (Array.isArray(value)) {
          shaders = value.map((r: any) => ({ id: r.id, name: decodeHeader(r.header) }))
          renderList()
        }
      }
    }
  }
}
