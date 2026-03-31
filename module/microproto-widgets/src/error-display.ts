import { WidgetFactory, PropertySchema, MicroProtoClient, Widget, TYPE } from './core/types'

/** Widget ID 3 for Text (LIST<uint8_t>) type */
export const ErrorDisplay: WidgetFactory = {
  types: [TYPE.LIST],
  widgetId: 3,
  complex: true,

  create(schema: PropertySchema, _client: MicroProtoClient): Widget {
    const el = document.createElement('div')
    el.style.cssText = 'display:none;padding:8px 12px;background:#dc2626;color:white;font-size:12px;border-radius:8px;font-family:monospace;white-space:pre-wrap;margin-bottom:12px'

    return {
      element: el,
      update(value: any) {
        const err = Array.isArray(value)
          ? String.fromCharCode(...value.filter((b: number) => b))
          : String(value || '')
        if (err) {
          el.textContent = err
          el.style.display = 'block'
        } else {
          el.style.display = 'none'
        }
      }
    }
  }
}
