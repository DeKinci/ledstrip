/**
 * IIFE entry point for the device-embedded widget bundle.
 * Built as a standalone script that expects MicroProtoClient as a global.
 * Loaded via <script src="/js/ui.js"> after <script src="/js/proto.js">.
 *
 * Exports window.MicroProtoUI = { renderAll }
 */

import { register, renderAll, Slider, Toggle, ErrorDisplay, CodeEditor, LedCanvas, SegmentEditor, HueSlider } from '@microproto/widgets'

register(Slider, Toggle, ErrorDisplay, CodeEditor, LedCanvas, SegmentEditor, HueSlider)

;(window as any).MicroProtoUI = { renderAll }
