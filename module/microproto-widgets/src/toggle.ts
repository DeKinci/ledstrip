import { WidgetFactory, PropertySchema, MicroProtoClient, Widget, TYPE } from './core/types'

export const Toggle: WidgetFactory = {
  types: [TYPE.BOOL],
  widgetId: 0,

  create(schema: PropertySchema, client: MicroProtoClient): Widget {
    const el = document.createElement('div')
    el.className = 'mp-control'
    el.style.cssText = 'background:#1a1a1a;border-radius:12px;padding:16px;border-left:3px solid ' + (schema.ui.colorHex || '#666') + ';margin-bottom:12px'

    el.innerHTML = `
      <div style="display:flex;align-items:center;gap:8px;margin-bottom:12px">
        <span style="font-size:18px">${schema.ui.icon || ''}</span>
        <span style="font-weight:600">${schema.name}</span>
      </div>
      <div style="display:flex;align-items:center;gap:12px">
        <label style="position:relative;width:48px;height:26px">
          <input type="checkbox" style="opacity:0;width:0;height:0" id="cb">
          <span style="position:absolute;inset:0;background:#333;border-radius:13px;cursor:pointer;transition:background 0.2s" id="track"></span>
        </label>
        <span style="font-size:13px;color:#888">${schema.description || ''}</span>
      </div>
    `

    const cb = el.querySelector('#cb') as HTMLInputElement
    const track = el.querySelector('#track') as HTMLElement

    // Add thumb via CSS
    track.style.cssText += ';position:absolute;inset:0;background:#333;border-radius:13px;cursor:pointer;transition:background 0.2s'
    const thumb = document.createElement('span')
    thumb.style.cssText = 'position:absolute;width:20px;height:20px;left:3px;bottom:3px;background:white;border-radius:50%;transition:transform 0.2s'
    track.appendChild(thumb)

    function render(checked: boolean) {
      track.style.background = checked ? '#22c55e' : '#333'
      thumb.style.transform = checked ? 'translateX(22px)' : 'none'
    }

    cb.onchange = () => {
      render(cb.checked)
      client.setProperty(schema.name, cb.checked)
    }

    if (schema.readonly) cb.disabled = true

    return {
      element: el,
      update(value: any) {
        cb.checked = !!value
        render(!!value)
      }
    }
  }
}
