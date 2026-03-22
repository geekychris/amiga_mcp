#!/usr/bin/env python3
"""Capture screenshots of each web UI tab for documentation."""

import asyncio
from playwright.async_api import async_playwright
import os

OUTPUT_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), "doc_images")
URL = "http://localhost:3000/"

# Tabs to capture and their selectors
TABS = [
    ("dashboard", '[data-tab="dashboard"]', None, "dashboard.png"),
    ("logs", '[data-tab="logs"]', None, "logs.png"),
    ("files", '[data-tab="files"]', None, "files.png"),
    ("tools_memory", '[data-tab="tools"]', '[data-subtab="memory"]', "tools_memory.png"),
    ("tools_variables", '[data-tab="tools"]', '[data-subtab="variables"]', "tools_variables.png"),
    ("tools_shell", '[data-tab="tools"]', '[data-subtab="shell"]', "tools_shell.png"),
    ("tools_tasks", '[data-tab="tools"]', '[data-subtab="tasks"]', "tools_tasks.png"),
    ("inspect_graphics", '[data-tab="inspect"]', '[data-subtab="graphics"]', "inspect_graphics.png"),
    ("inspect_system", '[data-tab="inspect"]', '[data-subtab="system"]', "inspect_system.png"),
    ("inspect_debug", '[data-tab="inspect"]', '[data-subtab="debug"]', "inspect_debug.png"),
    ("develop", '[data-tab="develop"]', None, "develop.png"),
    ("debugger", '[data-tab="debugger"]', None, "debugger.png"),
    ("settings", '[data-tab="settings"]', None, "settings.png"),
]

async def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    async with async_playwright() as p:
        browser = await p.chromium.launch()
        page = await browser.new_page(viewport={"width": 1280, "height": 900})

        await page.goto(URL)
        await page.wait_for_load_state("domcontentloaded")
        await asyncio.sleep(3)  # Let SSE events populate data

        for name, tab_sel, subtab_sel, filename in TABS:
            print(f"Capturing {name}...")

            # Click main tab
            tab = page.locator(tab_sel)
            if await tab.count() > 0:
                await tab.first.click()
                await asyncio.sleep(0.5)

            # Click sub-tab if specified
            if subtab_sel:
                subtab = page.locator(subtab_sel)
                if await subtab.count() > 0:
                    await subtab.first.click()
                    await asyncio.sleep(0.3)

            filepath = os.path.join(OUTPUT_DIR, filename)
            await page.screenshot(path=filepath, full_page=False)
            print(f"  -> {filepath}")

        # Capture the About dialog - scroll to SACC section
        print("Capturing about dialog...")
        about_btn = page.locator('text="About"')
        if await about_btn.count() > 0:
            await about_btn.first.click()
            await asyncio.sleep(0.5)
            await page.screenshot(path=os.path.join(OUTPUT_DIR, "about.png"), full_page=False)
            print(f"  -> about.png")

            # Scroll to SACC section and capture
            sacc = page.locator('text="Sacramento Amiga Computer Club"')
            if await sacc.count() > 0:
                await sacc.scroll_into_view_if_needed()
                await asyncio.sleep(0.3)
                await page.screenshot(path=os.path.join(OUTPUT_DIR, "about_sacc.png"), full_page=False)
                print(f"  -> about_sacc.png")

        await browser.close()

    print(f"\nDone! {len(TABS) + 1} screenshots saved to {OUTPUT_DIR}")

asyncio.run(main())
