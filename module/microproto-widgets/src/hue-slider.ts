import { WidgetFactory, PropertySchema, MicroProtoClient, Widget, TYPE } from './core/types'

/** Widget ID 1 for Number type — horizontal hue strip slider */
export const HueSlider: WidgetFactory = {
  types: [TYPE.UINT8],
  widgetId: 3,  // Widget::Number::HUE_SLIDER

  create(schema: PropertySchema, client: MicroProtoClient): Widget {
    const el = document.createElement('div')
    el.className = 'mp-control'
    el.style.cssText = 'background:#1a1a1a;border-radius:12px;padding:16px;border-left:3px solid ' + (schema.ui.colorHex || '#666') + ';margin-bottom:12px'

    const header = document.createElement('div')
    header.style.cssText = 'display:flex;align-items:center;gap:8px;margin-bottom:12px'
    header.innerHTML = `
      <span style="font-size:18px">${schema.ui.icon || ''}</span>
      <span style="font-weight:600">${schema.name}</span>
      <span style="margin-left:auto;font-size:14px;opacity:0.7;min-width:40px;text-align:right" id="val"></span>
    `
    el.appendChild(header)

    // Rainbow gradient slider
    const input = document.createElement('input')
    input.type = 'range'
    input.min = '0'
    input.max = '255'
    input.step = '1'
    input.style.cssText = `
      width:100%;height:16px;border-radius:8px;outline:none;-webkit-appearance:none;
      background:linear-gradient(to right,
        hsl(0,100%,50%),hsl(30,100%,50%),hsl(60,100%,50%),hsl(90,100%,50%),
        hsl(120,100%,50%),hsl(150,100%,50%),hsl(180,100%,50%),hsl(210,100%,50%),
        hsl(240,100%,50%),hsl(270,100%,50%),hsl(300,100%,50%),hsl(330,100%,50%),
        hsl(360,100%,50%));
    `

    if (schema.readonly) input.disabled = true

    const valSpan = header.querySelector('#val') as HTMLElement

    // Color preview dot
    const preview = document.createElement('div')
    preview.style.cssText = 'width:16px;height:16px;border-radius:50%;border:2px solid #333;margin-left:8px;flex-shrink:0'
    header.insertBefore(preview, valSpan)

    function updatePreview(hue: number) {
      // FastLED hue: 0-255 maps to 0-360 degrees
      preview.style.background = `hsl(${hue * 360 / 256}, 100%, 50%)`
    }

    input.oninput = () => {
      const v = parseInt(input.value)
      valSpan.textContent = String(v)
      updatePreview(v)
      client.setProperty(schema.name, v)
    }

    el.appendChild(input)

    return {
      element: el,
      update(value: any) {
        input.value = String(value)
        valSpan.textContent = String(value)
        updatePreview(Number(value))
      }
    }
  }
}
