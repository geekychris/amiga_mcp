"""Deploy built Amiga binaries to AmiKit shared folder."""

from __future__ import annotations

import logging
import shutil
from dataclasses import dataclass
from pathlib import Path

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

    def __init__(self, project_root: str | None = None, deploy_dir: str | None = None) -> None:
        if project_root:
            self._root = Path(project_root).resolve()
        else:
            self._root = Path(__file__).resolve().parent.parent.parent
        self._deploy_dir = self._resolve_deploy_dir(deploy_dir)

    def _resolve_deploy_dir(self, deploy_dir: str | None) -> Path | None:
        if deploy_dir:
            p = Path(deploy_dir).expanduser().resolve()
            if p.is_dir():
                return p
            logger.warning("Deploy dir does not exist: %s", p)
            return None

        for candidate in AMIKIT_DEPLOY_CANDIDATES:
            p = Path(candidate).expanduser().resolve()
            if p.is_dir():
                logger.info("Auto-detected deploy dir: %s", p)
                return p

        logger.info("No deploy directory found; deploy will fail until configured")
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
        """Copy a file to the deploy directory."""
        if self._deploy_dir is None:
            return None
        dest = self._deploy_dir / dest_name
        try:
            shutil.copy2(src, dest)
            logger.info("Deployed: %s -> %s", src, dest)
            return str(dest)
        except Exception as e:
            logger.error("Deploy failed: %s -> %s: %s", src, dest, e)
            return None
