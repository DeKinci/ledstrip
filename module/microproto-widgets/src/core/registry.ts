import { WidgetFactory, PropertySchema, MicroProtoClient, Widget, TYPE } from './types'

/** Registry maps (typeId, widgetId) → WidgetFactory */
const factories = new Map<string, WidgetFactory>()

/** Track which factories are "complex" (go in right column) */
const complexTypes = new Set<string>()

function key(typeId: number, widgetId: number): string {
  return `${typeId}:${widgetId}`
}

/** Register a widget factory. Pass complex=true for widgets that go in the right column. */
export function register(...widgetFactories: WidgetFactory[]) {
  for (const f of widgetFactories) {
    for (const t of f.types) {
      factories.set(key(t, f.widgetId), f)
      if (f.complex) complexTypes.add(key(t, f.widgetId))
    }
  }
}

/** Look up widget factory for a property */
export function resolve(schema: PropertySchema): WidgetFactory | null {
  const wid = schema.ui?.widget || 0
  const exact = factories.get(key(schema.typeId, wid))
  if (exact) return exact
  if (wid !== 0) {
    return factories.get(key(schema.typeId, 0)) || null
  }
  return null
}

function isComplex(schema: PropertySchema): boolean {
  const wid = schema.ui?.widget || 0
  return complexTypes.has(key(schema.typeId, wid))
}

/** Render all properties from a MicroProto client into a container */
export function renderAll(container: HTMLElement, client: MicroProtoClient, options?: {
  layout?: 'single' | 'two-column'
  filter?: (schema: PropertySchema) => boolean
}) {
  const widgets = new Map<string, Widget>()
  const layout = options?.layout || 'single'
  const filter = options?.filter || (() => true)

  let leftCol: HTMLElement
  let rightCol: HTMLElement

  if (layout === 'two-column') {
    container.style.display = 'flex'
    container.style.gap = '16px'
    leftCol = document.createElement('div')
    leftCol.style.cssText = 'width:33%;min-width:220px;flex-shrink:0'
    rightCol = document.createElement('div')
    rightCol.style.cssText = 'flex:1;min-width:0'
    container.appendChild(leftCol)
    container.appendChild(rightCol)
  } else {
    leftCol = container
    rightCol = container
  }

  client.on('schema', (schema: PropertySchema) => {
    if (schema.hidden || !filter(schema)) return

    const factory = resolve(schema)
    if (!factory) return

    // Remove existing widget if re-syncing
    const existing = widgets.get(schema.name)
    if (existing) {
      existing.destroy?.()
      existing.element.remove()
    }

    const widget = factory.create(schema, client)
    widgets.set(schema.name, widget)

    const target = isComplex(schema) ? rightCol : leftCol
    target.appendChild(widget.element)
  })

  client.on('property', (_id: number, name: string, value: any, oldValue: any) => {
    const widget = widgets.get(name)
    if (widget) widget.update(value, oldValue)
  })

  return {
    destroy() {
      for (const w of widgets.values()) w.destroy?.()
      widgets.clear()
      container.innerHTML = ''
    }
  }
}
