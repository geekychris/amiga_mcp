"""Docker-based Amiga cross-compilation builder."""

from __future__ import annotations

import asyncio
import logging
import time
from dataclasses import dataclass
from pathlib import Path

logger = logging.getLogger(__name__)

DOCKER_IMAGE = "amigadev/crosstools:m68k-amigaos"


@dataclass
class BuildResult:
    success: bool
    output: str
    errors: str
    duration: int  # milliseconds


class Builder:
    """Build Amiga projects via Docker cross-compiler."""

    def __init__(self, project_root: str | None = None) -> None:
        if project_root:
            self._root = str(Path(project_root).resolve())
        else:
            # Default: parent of amiga-devbench directory
            self._root = str(Path(__file__).resolve().parent.parent.parent)
        self.last_build_result: BuildResult | None = None

    async def build(self, project: str | None = None) -> BuildResult:
        start = time.monotonic()

        if project:
            cmd = [
                "docker", "run", "--rm",
                "-v", f"{self._root}:/work", "-w", "/work",
                DOCKER_IMAGE,
                "make", "-C", project,
            ]
        else:
            cmd = [
                "docker", "run", "--rm",
                "-v", f"{self._root}:/work", "-w", "/work",
                DOCKER_IMAGE,
                "sh", "-c",
                "make -C amiga-debug-lib && make -C examples/hello_world && make -C examples/bouncing_ball",
            ]

        try:
            proc = await asyncio.create_subprocess_exec(
                *cmd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
            stdout_bytes, stderr_bytes = await asyncio.wait_for(
                proc.communicate(), timeout=120.0,
            )
            duration = int((time.monotonic() - start) * 1000)
            stdout = stdout_bytes.decode("utf-8", errors="replace")
            stderr = stderr_bytes.decode("utf-8", errors="replace")

            self.last_build_result = BuildResult(
                success=(proc.returncode == 0),
                output=stdout,
                errors=stderr,
                duration=duration,
            )
        except asyncio.TimeoutError:
            duration = int((time.monotonic() - start) * 1000)
            self.last_build_result = BuildResult(
                success=False, output="", errors="Build timed out (120s)", duration=duration,
            )
        except Exception as e:
            duration = int((time.monotonic() - start) * 1000)
            self.last_build_result = BuildResult(
                success=False, output="", errors=str(e), duration=duration,
            )

        return self.last_build_result

    async def clean(self, project: str | None = None) -> BuildResult:
        start = time.monotonic()
        target = project or "amiga-debug-lib"

        cmd = [
            "docker", "run", "--rm",
            "-v", f"{self._root}:/work", "-w", "/work",
            DOCKER_IMAGE,
            "make", "-C", target, "clean",
        ]

        try:
            proc = await asyncio.create_subprocess_exec(
                *cmd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
            stdout_bytes, stderr_bytes = await asyncio.wait_for(
                proc.communicate(), timeout=60.0,
            )
            duration = int((time.monotonic() - start) * 1000)
            stdout = stdout_bytes.decode("utf-8", errors="replace")
            stderr = stderr_bytes.decode("utf-8", errors="replace")

            self.last_build_result = BuildResult(
                success=(proc.returncode == 0),
                output=stdout,
                errors=stderr,
                duration=duration,
            )
        except asyncio.TimeoutError:
            duration = int((time.monotonic() - start) * 1000)
            self.last_build_result = BuildResult(
                success=False, output="", errors="Clean timed out (60s)", duration=duration,
            )
        except Exception as e:
            duration = int((time.monotonic() - start) * 1000)
            self.last_build_result = BuildResult(
                success=False, output="", errors=str(e), duration=duration,
            )

        return self.last_build_result
