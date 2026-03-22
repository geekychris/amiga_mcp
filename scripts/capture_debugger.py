#!/usr/bin/env python3
"""Capture a debugger screenshot with an active debug session."""

import asyncio
from playwright.async_api import async_playwright
import os

OUTPUT_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), "doc_images")

async def main():
    async with async_playwright() as p:
        browser = await p.chromium.launch()
        page = await browser.new_page(viewport={"width": 1280, "height": 900})

        await page.goto("http://localhost:3000/")
        await page.wait_for_load_state("domcontentloaded")
        await asyncio.sleep(3)

        # Go to debugger tab
        print("Navigating to debugger tab...")
        await page.click('[data-tab="debugger"]')
        await asyncio.sleep(1)

        # Select bouncing_ball target
        target = page.locator("#dbg-target")
        options = await target.locator("option").all_text_contents()
        print(f"Targets: {options}")

        selected = False
        for opt in options:
            if "bouncing_ball" in opt:
                await target.select_option(label=opt)
                print(f"Selected: {opt}")
                selected = True
                break

        if not selected:
            print("bouncing_ball not found, using first non-empty option")
            for opt in options:
                if opt.strip() and opt != "Select target...":
                    await target.select_option(label=opt)
                    break

        await asyncio.sleep(0.5)

        # Click Build & Launch
        print("Clicking Build & Launch...")
        build_btns = page.locator("button")
        count = await build_btns.count()
        for i in range(count):
            btn = build_btns.nth(i)
            text = await btn.text_content()
            if text and "Build" in text and "Launch" in text:
                await btn.click()
                print(f"Clicked: {text}")
                break

        # Wait for build + deploy + launch + symbol load
        print("Waiting for build and launch (20s)...")
        await asyncio.sleep(20)

        # Take initial screenshot to see what state we're in
        await page.screenshot(path=os.path.join(OUTPUT_DIR, "debugger_building.png"))
        print("Saved debugger_building.png")

        # Try clicking Break button to stop execution
        print("Trying to break execution...")
        break_btns = page.locator("button")
        count = await break_btns.count()
        for i in range(count):
            btn = break_btns.nth(i)
            text = await btn.text_content()
            if text and text.strip() == "Break":
                try:
                    await btn.click(timeout=5000)
                    print("Clicked Break")
                except:
                    print("Break button not clickable")
                break

        await asyncio.sleep(3)

        # Try Step Into to get source view
        print("Trying Step Into...")
        for i in range(count):
            btn = break_btns.nth(i)
            text = await btn.text_content()
            if text and "Step Into" in text:
                try:
                    await btn.click(timeout=5000)
                    print("Clicked Step Into")
                except:
                    print("Step Into not clickable")
                break

        await asyncio.sleep(2)

        # Capture the debugger with active session
        filepath = os.path.join(OUTPUT_DIR, "debugger.png")
        await page.screenshot(path=filepath)
        print(f"Saved {filepath}")

        await browser.close()

asyncio.run(main())
