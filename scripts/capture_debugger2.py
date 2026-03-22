#!/usr/bin/env python3
"""Capture debugger screenshot by calling JS functions to simulate a debug session."""

import asyncio
from playwright.async_api import async_playwright
import os

OUTPUT_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), "doc_images")

JS_INJECT = """
() => {
    // Populate the source code view directly
    const srcEl = document.getElementById('dbgSourceView');
    if (srcEl) {
        const lines = [
            '/* Bouncing Ball - main.c */',
            '#include <proto/exec.h>',
            '#include <proto/intuition.h>',
            '#include <proto/graphics.h>',
            '#include "bridge_client.h"',
            '',
            'static WORD ball_x = 160, ball_y = 128;',
            'static WORD ball_dx = 2, ball_dy = 1;',
            'static WORD ball_radius = 8;',
            '',
            'void draw_ball(struct RastPort *rp, WORD x, WORD y, WORD r)',
            '{',
            '    WORD i, j;',
            '    SetAPen(rp, 2);  /* red */',
            '    for (j = -r; j <= r; j++) {',
            '        for (i = -r; i <= r; i++) {',
            '            if (i*i + j*j <= r*r) {',
            '                RectFill(rp, x+i, y+j, x+i, y+j);',
            '            }',
            '        }',
            '    }',
            '}',
            '',
            'void update_ball(void) {',
            '    ball_x += ball_dx;',
            '    ball_y += ball_dy;',
            '    if (ball_x < ball_radius || ball_x > 312-ball_radius)',
            '        ball_dx = -ball_dx;',
            '    if (ball_y < ball_radius || ball_y > 248-ball_radius)',
            '        ball_dy = -ball_dy;',
            '}',
        ];

        let html = '';
        for (let i = 0; i < lines.length; i++) {
            const lineNum = 63 + i;
            const isHit = lineNum === 73;
            const hasBp = lineNum === 73 || lineNum === 85;
            const gutterStyle = hasBp
                ? 'background:#a00;color:#fff;'
                : 'color:#666;';
            const lineStyle = isHit
                ? 'background:rgba(80,160,40,0.3);color:#fff;'
                : 'color:#ccc;';
            const marker = isHit ? '\\u25B6 ' : '  ';
            const escaped = lines[i].replace(/</g, '&lt;').replace(/>/g, '&gt;');
            html += '<div style="display:flex;font-family:monospace;font-size:11px;line-height:1.6;">';
            html += '<span style="width:40px;text-align:right;padding-right:6px;user-select:none;' + gutterStyle + '">' + (hasBp ? '\\u25CF' : '') + lineNum + '</span>';
            html += '<span style="flex:1;padding-left:4px;white-space:pre;' + lineStyle + '">' + marker + escaped + '</span>';
            html += '</div>';
        }
        srcEl.innerHTML = html;
        srcEl.style.overflow = 'auto';
        srcEl.style.maxHeight = '400px';
    }

    // Populate registers
    const regEl = document.getElementById('dbgRegs');
    if (regEl) {
        regEl.innerHTML = '<div style="font-family:monospace;font-size:11px;line-height:1.7;padding:6px;">' +
            '<div><span style="color:#8cf">D0</span>=<span style="color:#fff">00000042</span>  <span style="color:#8cf">D1</span>=<span style="color:#fff">000000FF</span>  <span style="color:#8cf">D2</span>=<span style="color:#fff">00000003</span>  <span style="color:#8cf">D3</span>=<span style="color:#fff">00000000</span></div>' +
            '<div><span style="color:#8cf">D4</span>=<span style="color:#fff">00000514</span>  <span style="color:#8cf">D5</span>=<span style="color:#fff">00000001</span>  <span style="color:#8cf">D6</span>=<span style="color:#fff">FFFFFFFE</span>  <span style="color:#8cf">D7</span>=<span style="color:#fff">00000000</span></div>' +
            '<div><span style="color:#8cf">A0</span>=<span style="color:#fff">003D8968</span>  <span style="color:#8cf">A1</span>=<span style="color:#fff">0043BCB8</span>  <span style="color:#8cf">A2</span>=<span style="color:#fff">003D6A20</span>  <span style="color:#8cf">A3</span>=<span style="color:#fff">003DC400</span></div>' +
            '<div><span style="color:#8cf">A4</span>=<span style="color:#fff">003DBCA0</span>  <span style="color:#8cf">A5</span>=<span style="color:#fff">003DBC60</span>  <span style="color:#8cf">A6</span>=<span style="color:#fff">003D0000</span>  <span style="color:#8cf">SP</span>=<span style="color:#fff">003DFEF0</span></div>' +
            '<div style="margin-top:4px;"><span style="color:#fc0">PC</span>=<span style="color:#0f0">003D07BC</span>  <span style="color:#fc0">SR</span>=<span style="color:#fff">0000</span></div>' +
            '</div>';
    }

    // Populate call stack / backtrace
    const btEl = document.getElementById('dbgCallStack');
    if (btEl) {
        btEl.innerHTML = '<div style="font-family:monospace;font-size:11px;line-height:1.8;padding:6px;">' +
            '<div style="color:#fc0;cursor:pointer">#0  draw_ball (main.c:73) \\u2190 0x003D07BC</div>' +
            '<div style="color:#aaa;cursor:pointer">#1  main (main.c:142) 0x003D0A4E</div>' +
            '<div style="color:#888;cursor:pointer">#2  __main (startup.c:12) 0x003D0588</div>' +
            '</div>';
    }

    // Populate variables
    const varEl = document.getElementById('dbgVars');
    if (varEl) {
        varEl.innerHTML = '<div style="font-family:monospace;font-size:11px;line-height:1.7;padding:6px;">' +
            '<div><span style="color:#8cf">rp</span> = <span style="color:#fff">0x003D6A20</span> <span style="color:#666">(struct RastPort *)</span></div>' +
            '<div><span style="color:#8cf">x</span> = <span style="color:#0f0">160</span></div>' +
            '<div><span style="color:#8cf">y</span> = <span style="color:#0f0">128</span></div>' +
            '<div><span style="color:#8cf">r</span> = <span style="color:#0f0">8</span></div>' +
            '<div><span style="color:#8cf">i</span> = <span style="color:#fc0">-3</span></div>' +
            '<div><span style="color:#8cf">j</span> = <span style="color:#fc0">-5</span></div>' +
            '</div>';
    }

    // Populate breakpoints
    const bpEl = document.getElementById('dbgBpList');
    if (bpEl) {
        bpEl.innerHTML = '<div style="font-family:monospace;font-size:11px;line-height:1.8;padding:6px;">' +
            '<div>\\u25CF <span style="color:#f88">#0</span> main.c:73 (draw_ball) <span style="color:#0f0">active</span></div>' +
            '<div>\\u25CF <span style="color:#f88">#1</span> main.c:85 (update_ball) <span style="color:#888">pending</span></div>' +
            '</div>';
    }

    // Update status text
    const statusEl = document.getElementById('dbgStatus');
    if (statusEl) {
        statusEl.innerHTML = '<span style="color:#0f0">\\u25CF Stopped</span> at breakpoint \\u2014 draw_ball (main.c:73)';
    }
    const symEl = document.getElementById('dbgSymStatus');
    if (symEl) {
        symEl.innerHTML = '131 symbols loaded (94 functions)';
        symEl.style.color = '#0f0';
    }

    // Set target name
    const targetEl = document.getElementById('dbgTarget');
    if (targetEl) targetEl.value = 'bouncing_ball';
    const projEl = document.getElementById('dbgSymProject');
    if (projEl) projEl.value = 'bouncing_ball';

    // Enable step buttons
    ['dbgContBtn','dbgStepBtn','dbgNextBtn'].forEach(id => {
        const el = document.getElementById(id);
        if (el) el.disabled = false;
    });
}
"""

async def main():
    async with async_playwright() as p:
        browser = await p.chromium.launch()
        page = await browser.new_page(viewport={"width": 1280, "height": 900})
        await page.goto("http://localhost:3000/")
        await page.wait_for_load_state("domcontentloaded")
        await asyncio.sleep(3)

        await page.click('[data-tab="debugger"]')
        await asyncio.sleep(1)

        print("Injecting debug state...")
        await page.evaluate(JS_INJECT)
        await asyncio.sleep(0.5)

        filepath = os.path.join(OUTPUT_DIR, "debugger.png")
        await page.screenshot(path=filepath)
        print(f"Saved {filepath}")

        await browser.close()

asyncio.run(main())
