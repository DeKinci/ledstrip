import { WidgetFactory, PropertySchema, MicroProtoClient, Widget, TYPE } from './core/types'

export const Slider: WidgetFactory = {
  types: [TYPE.UINT8, TYPE.INT8, TYPE.INT16, TYPE.UINT16, TYPE.INT32, TYPE.FLOAT32],
  widgetId: 0,  // default for numeric types

  create(schema: PropertySchema, client: MicroProtoClient): Widget {
    const isFloat = schema.typeId === TYPE.FLOAT32
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

    const input = document.createElement('input')
    input.type = 'range'
    input.style.cssText = 'width:100%;height:8px;border-radius:4px;background:#333;outline:none;-webkit-appearance:none'
    input.min = String(schema.constraints.hasMin ? schema.constraints.min : 0)
    input.max = String(schema.constraints.hasMax ? schema.constraints.max : 255)
    if (schema.constraints.hasStep) input.step = String(schema.constraints.step)
    else if (isFloat) input.step = String((Number(input.max) - Number(input.min)) / 1000)

    if (schema.readonly) input.disabled = true

    const valSpan = header.querySelector('#val') as HTMLElement

    input.oninput = () => {
      const v = isFloat ? parseFloat(input.value) : parseInt(input.value)
      valSpan.textContent = isFloat ? v.toFixed(2) : String(v)
      client.setProperty(schema.name, v)
    }

    el.appendChild(input)

    return {
      element: el,
      update(value: any) {
        input.value = String(value)
        valSpan.textContent = typeof value === 'number' && !Number.isInteger(value) ? value.toFixed(2) : String(value)
      }
    }
  }
}
