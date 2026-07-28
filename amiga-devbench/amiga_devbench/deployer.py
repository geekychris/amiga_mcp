"""Deploy built Amiga binaries to AmiKit shared folder.

Two paths exist:
- ``deploy()``: the traditional shared-folder copy. Fast (host filesystem
  write) but requires the emulator (or the Amiga) to see the folder via a
  mount / shared-directory config.
- ``deploy_via_bridge()``: an async path that streams the file over the
  bridge protocol using :mod:`file_transfer`. Works whenever the bridge is
  connected — including remote emulators (pi-amikit) and real hardware on
  the LAN.

``deploy_smart()`` picks between them automatically.
"""

from __future__ import annotations

import asyncio
import logging
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Any

logger = logging.getLogger(__name__)

# Common AmiKit shared folder locations
AMIKIT_DEPLOY_CANDIDATES = [
    # macOS AmiKit Dropbox/Dev
    "/Applications/AmiKit.app/Contents/SharedSupport/prefix/drive_c/AmiKit/Dropbox/Dev",
    # User-level paths
    "~/AmiKit/Dropbox/Dev",
    "~/Documents/AmiKit/Dev",
]


@dataclass
class DeployResult:
    success: bool
    message: str
    files: list[str]


class Deployer:
    """Copy built binaries to AmiKit shared folder."""

    def __init__(
        self,
        project_root: str | None = None,
        deploy_dir: str | None = None,
        force_bridge: bool = False,
    ) -> None:
        if project_root:
            self._root = Path(project_root).resolve()
        else:
            self._root = Path(__file__).resolve().parent.parent.parent
        self._is_hdf = False  # set by _resolve_deploy_dir
        self._deploy_dir = self._resolve_deploy_dir(deploy_dir)
        # When the emulator is on another host (profile has ``emulator_ssh``),
        # the local shared folder — even if auto-detected — points somewhere
        # the target Amiga can't see. Callers set force_bridge=True in that
        # case so deploy_smart always routes over the bridge.
        self._force_bridge = force_bridge

    def _resolve_deploy_dir(self, deploy_dir: str | None) -> Path | None:
        if deploy_dir:
            p = Path(deploy_dir).expanduser().resolve()
            # An .hdf path is a hardfile — write via xdftool, not shutil.
            # See _copy_file: presence of _is_hdf routes through the
            # scripts/deploy-os4.sh wrapper.
            if p.suffix.lower() == ".hdf" and p.is_file():
                self._is_hdf = True
                logger.info("Deploy target is a hardfile: %s (will use xdftool)", p)
                return p
            if p.is_dir():
                self._is_hdf = False
                return p
            logger.warning("Deploy dir does not exist: %s", p)
            return None

        for candidate in AMIKIT_DEPLOY_CANDIDATES:
            p = Path(candidate).expanduser().resolve()
            if p.is_dir():
                logger.info("Auto-detected deploy dir: %s", p)
                self._is_hdf = False
                return p

        logger.info("No deploy directory found; deploy will fail until configured")
        self._is_hdf = False
        return None

    def deploy(self, project: str | None = None) -> DeployResult:
        """Deploy built binaries. If project is given, deploy that project's binary.
        Otherwise deploy all example binaries found."""
        if self._deploy_dir is None:
            return DeployResult(
                success=False,
                message="No deploy directory configured. Use --deploy-dir or install AmiKit.",
                files=[],
            )

        deployed: list[str] = []
        errors: list[str] = []

        if project:
            # Deploy a specific project
            binary = self._find_binary(project)
            if binary:
                result = self._copy_file(binary, binary.name)
                if result:
                    deployed.append(result)
                else:
                    errors.append(f"Failed to copy {binary}")
            else:
                errors.append(f"No binary found for project: {project}")
        else:
            # Deploy all example binaries
            examples_dir = self._root / "examples"
            if examples_dir.is_dir():
                for subdir in sorted(examples_dir.iterdir()):
                    if subdir.is_dir():
                        binary = self._find_binary(f"examples/{subdir.name}")
                        if binary:
                            result = self._copy_file(binary, binary.name)
                            if result:
                                deployed.append(result)

        if errors:
            return DeployResult(
                success=False,
                message="; ".join(errors),
                files=deployed,
            )

        if not deployed:
            return DeployResult(
                success=False,
                message="No binaries found to deploy",
                files=[],
            )

        return DeployResult(
            success=True,
            message=f"Deployed {len(deployed)} file(s) to {self._deploy_dir}",
            files=deployed,
        )

    def deploy_file(self, src: str, dest_name: str) -> DeployResult:
        """Deploy a specific file to the shared folder."""
        if self._deploy_dir is None:
            return DeployResult(success=False, message="No deploy directory configured", files=[])

        src_path = Path(src)
        if not src_path.is_file():
            return DeployResult(success=False, message=f"Source file not found: {src}", files=[])

        result = self._copy_file(src_path, dest_name)
        if result:
            return DeployResult(success=True, message=f"Deployed {dest_name}", files=[result])
        return DeployResult(success=False, message=f"Failed to copy {src}", files=[])

    def _find_binary(self, project: str) -> Path | None:
        """Find the built binary for a project."""
        project_dir = self._root / project
        if not project_dir.is_dir():
            return None

        # Look for common binary names (the project directory name without path)
        name = project_dir.name
        for candidate in [name, name.replace("_", "-"), name.upper()]:
            binary = project_dir / candidate
            if binary.is_file():
                return binary

        # Look for any executable file
        for f in project_dir.iterdir():
            if f.is_file() and not f.suffix and f.stat().st_size > 0:
                return f

        return None

    def _copy_file(self, src: Path, dest_name: str) -> str | None:
        """Copy a file to the deploy target — either a shared folder or
        an .hdf hardfile (via scripts/deploy-os4.sh → xdftool)."""
        if self._deploy_dir is None:
            return None
        if self._is_hdf:
            return self._copy_file_hdf(src, dest_name)
        dest = self._deploy_dir / dest_name
        try:
            shutil.copy2(src, dest)
            logger.info("Deployed: %s -> %s", src, dest)
            return str(dest)
        except Exception as e:
            logger.error("Deploy failed: %s -> %s: %s", src, dest, e)
            return None

    def _copy_file_hdf(self, src: Path, dest_name: str) -> str | None:
        """Write ``src`` into the deploy HDF as ``dest_name``.

        Uses ``scripts/deploy-os4.sh`` (which shells out to xdftool). The
        script must live at ``<project_root>/scripts/deploy-os4.sh``. If
        QEMU has the HDF open the write will still land on disk but OS4's
        cached view of the drive may be stale until a reboot.
        """
        import subprocess
        script = self._root / "scripts" / "deploy-os4.sh"
        if not script.is_file():
            logger.error("deploy-os4.sh not found at %s", script)
            return None
        try:
            env = {"OS4_DEV_HDF": str(self._deploy_dir)}
            import os as _os
            env = {**_os.environ, **env}
            result = subprocess.run(
                ["bash", str(script), str(src), dest_name],
                capture_output=True, text=True, timeout=30, env=env,
            )
            if result.returncode != 0:
                logger.error("HDF deploy failed: %s", result.stderr.strip())
                return None
            logger.info("Deployed to HDF: %s -> %s:%s",
                        src, self._deploy_dir, dest_name)
            return f"{self._deploy_dir}:{dest_name}"
        except Exception as e:
            logger.error("HDF deploy exception: %s", e)
            return None

    @property
    def has_shared_folder(self) -> bool:
        """True if a shared deploy dir is configured and writable.

        Returns False when ``force_bridge`` is set (typically because the
        target Amiga lives on another host and can't see this Mac's disk),
        so :meth:`deploy_smart` always takes the bridge path.
        """
        if self._force_bridge:
            return False
        if self._deploy_dir is None:
            return False
        try:
            import os
            if self._is_hdf:
                # HDF is a regular file; still writable via xdftool wrapper.
                return self._deploy_dir.is_file() and os.access(self._deploy_dir, os.W_OK)
            return self._deploy_dir.is_dir() and os.access(self._deploy_dir, os.W_OK)
        except Exception:
            return False

    async def deploy_via_bridge(
        self,
        project: str,
        conn: Any,
        bus: Any,
        amiga_dest: str = "DH2:Dev",
    ) -> DeployResult:
        """Stream the project's built binary to the Amiga over the bridge.

        Uses :mod:`file_transfer` (WRITEFILE + APPEND chunks, CRC32 verified).
        The caller must supply a connected SerialConnection and its EventBus.
        """
        binary = self._find_binary(project)
        if not binary:
            return DeployResult(
                success=False,
                message=f"No binary found for project: {project}",
                files=[],
            )
        amiga_path = amiga_dest.rstrip("/") + "/" + binary.name
        # Import locally so a purely-shared-folder deploy has no dependency
        # on the bridge module.
        from . import file_transfer
        result = await file_transfer.push_file(conn, bus, str(binary), amiga_path)
        if result.success:
            return DeployResult(
                success=True,
                message=f"Deployed via bridge -> {amiga_path} ({result.message})",
                files=[amiga_path],
            )
        return DeployResult(
            success=False,
            message=f"Bridge deploy failed: {result.message}",
            files=[],
        )

    async def deploy_smart(
        self,
        project: str | None,
        conn: Any = None,
        bus: Any = None,
        amiga_dest: str = "DH2:Dev",
    ) -> DeployResult:
        """Prefer the shared-folder path; fall back to the bridge when the
        shared folder isn't available or writable.

        For multi-file deploy (``project is None``) we only use the shared
        folder — the bridge path is per-file and doesn't currently loop the
        examples tree the way the shared-folder path does.
        """
        if self.has_shared_folder:
            logger.info("deploy_smart: using shared folder %s", self._deploy_dir)
            # self.deploy() is sync — for HDF targets it spawns a xdftool
            # subprocess with a 30-second timeout, which would freeze the
            # ASGI event loop. Hop to a worker thread so other requests
            # keep flowing.
            return await asyncio.to_thread(self.deploy, project)
        if project is None:
            return DeployResult(
                success=False,
                message=(
                    "No shared folder configured and no project specified. "
                    "Pass a specific project to deploy over the bridge."
                ),
                files=[],
            )
        if conn is None or bus is None or not getattr(conn, "connected", False):
            return DeployResult(
                success=False,
                message=(
                    "No shared folder configured and bridge is not connected. "
                    "Either set [paths] deploy_dir, or connect the Amiga bridge."
                ),
                files=[],
            )
        logger.info("deploy_smart: no shared folder — falling back to bridge")
        return await self.deploy_via_bridge(project, conn, bus, amiga_dest)
