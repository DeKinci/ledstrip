/** LED Strip profile — includes only widgets needed for the LED strip controller */
import { register, renderAll } from '../widgets/src/core/registry'
import { Slider } from '../widgets/src/slider'
import { Toggle } from '../widgets/src/toggle'
import { ErrorDisplay } from '../widgets/src/error-display'
import { CodeEditor } from '../widgets/src/code-editor'
import { LedCanvas } from '../widgets/src/led-canvas'
import { SegmentEditor } from '../widgets/src/segment-editor'

register(Slider, Toggle, ErrorDisplay, CodeEditor, LedCanvas, SegmentEditor)

// Export for use in HTML
;(window as any).MicroProtoUI = { renderAll }
