"""Docker-based Amiga cross-compilation builder."""

from __future__ import annotations

import asyncio
import logging
import time
from dataclasses import dataclass
from pathlib import Path

logger = logging.getLogger(__name__)

DOCKER_IMAGE = "amigadev/crosstools:m68k-amigaos"
CONTAINER_NAME = "amiga-builder"


@dataclass
class BuildResult:
    success: bool
    output: str
    errors: str
    duration: int  # milliseconds


class Builder:
    """Build Amiga projects via Docker cross-compiler.

    Uses a persistent Docker container to avoid startup overhead on each build.
    The container is created on first build and reused for subsequent builds.
    """

    def __init__(self, project_root: str | None = None) -> None:
        if project_root:
            self._root = str(Path(project_root).resolve())
        else:
            # Default: parent of amiga-devbench directory
            self._root = str(Path(__file__).resolve().parent.parent.parent)
        self.last_build_result: BuildResult | None = None
        self._container_ready = False

    async def _ensure_container(self) -> bool:
        """Ensure the persistent build container is running."""
        if self._container_ready:
            # Quick check that it's still there
            proc = await asyncio.create_subprocess_exec(
                "docker", "inspect", "-f", "{{.State.Running}}", CONTAINER_NAME,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
            stdout, _ = await proc.communicate()
            if proc.returncode == 0 and b"true" in stdout:
                return True
            self._container_ready = False

        # Remove stale container if exists
        proc = await asyncio.create_subprocess_exec(
            "docker", "rm", "-f", CONTAINER_NAME,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        await proc.communicate()

        # Start persistent container with sleep infinity
        proc = await asyncio.create_subprocess_exec(
            "docker", "run", "-d",
            "--name", CONTAINER_NAME,
            "-v", f"{self._root}:/work",
            "-w", "/work",
            DOCKER_IMAGE,
            "sleep", "infinity",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        stdout, stderr = await proc.communicate()
        if proc.returncode != 0:
            logger.error("Failed to start build container: %s",
                         stderr.decode("utf-8", errors="replace"))
            return False

        self._container_ready = True
        logger.info("Started persistent build container '%s'", CONTAINER_NAME)
        return True

    async def _exec_in_container(self, cmd: list[str], timeout: float = 120.0) -> BuildResult:
        """Execute a command in the persistent container."""
        start = time.monotonic()

        if not await self._ensure_container():
            # Fall back to docker run
            logger.warning("Container not available, falling back to docker run")
            return await self._docker_run(cmd, timeout)

        exec_cmd = ["docker", "exec", CONTAINER_NAME] + cmd

        try:
            proc = await asyncio.create_subprocess_exec(
                *exec_cmd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
            stdout_bytes, stderr_bytes = await asyncio.wait_for(
                proc.communicate(), timeout=timeout,
            )
            duration = int((time.monotonic() - start) * 1000)
            stdout = stdout_bytes.decode("utf-8", errors="replace")
            stderr = stderr_bytes.decode("utf-8", errors="replace")

            result = BuildResult(
                success=(proc.returncode == 0),
                output=stdout,
                errors=stderr,
                duration=duration,
            )
        except asyncio.TimeoutError:
            duration = int((time.monotonic() - start) * 1000)
            result = BuildResult(
                success=False, output="", errors=f"Build timed out ({timeout}s)", duration=duration,
            )
        except Exception as e:
            duration = int((time.monotonic() - start) * 1000)
            result = BuildResult(
                success=False, output="", errors=str(e), duration=duration,
            )

        self.last_build_result = result
        return result

    async def _docker_run(self, cmd: list[str], timeout: float = 120.0) -> BuildResult:
        """Fallback: run command via docker run --rm."""
        start = time.monotonic()
        run_cmd = [
            "docker", "run", "--rm",
            "-v", f"{self._root}:/work", "-w", "/work",
            DOCKER_IMAGE,
        ] + cmd

        try:
            proc = await asyncio.create_subprocess_exec(
                *run_cmd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
            stdout_bytes, stderr_bytes = await asyncio.wait_for(
                proc.communicate(), timeout=timeout,
            )
            duration = int((time.monotonic() - start) * 1000)
            stdout = stdout_bytes.decode("utf-8", errors="replace")
            stderr = stderr_bytes.decode("utf-8", errors="replace")

            result = BuildResult(
                success=(proc.returncode == 0),
                output=stdout,
                errors=stderr,
                duration=duration,
            )
        except asyncio.TimeoutError:
            duration = int((time.monotonic() - start) * 1000)
            result = BuildResult(
                success=False, output="", errors=f"Build timed out ({timeout}s)", duration=duration,
            )
        except Exception as e:
            duration = int((time.monotonic() - start) * 1000)
            result = BuildResult(
                success=False, output="", errors=str(e), duration=duration,
            )

        self.last_build_result = result
        return result

    async def build(self, project: str | None = None) -> BuildResult:
        if project:
            cmd = ["make", "-C", project]
        else:
            cmd = [
                "sh", "-c",
                "make -C amiga-debug-lib && make -C examples/hello_world && make -C examples/bouncing_ball",
            ]
        return await self._exec_in_container(cmd)

    async def clean(self, project: str | None = None) -> BuildResult:
        target = project or "amiga-debug-lib"
        cmd = ["make", "-C", target, "clean"]
        return await self._exec_in_container(cmd, timeout=60.0)

    async def shutdown(self) -> None:
        """Stop and remove the persistent container."""
        if self._container_ready:
            proc = await asyncio.create_subprocess_exec(
                "docker", "rm", "-f", CONTAINER_NAME,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
            await proc.communicate()
            self._container_ready = False
            logger.info("Stopped build container '%s'", CONTAINER_NAME)
