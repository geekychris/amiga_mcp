import { execFile } from "child_process";
import { promisify } from "util";
import { resolve } from "path";

const execFileAsync = promisify(execFile);

const DOCKER_IMAGE = "amigadev/crosstools:m68k-amigaos";

export interface BuildResult {
  success: boolean;
  output: string;
  errors: string;
  duration: number;
}

export class Builder {
  private projectRoot: string;
  lastBuildResult: BuildResult | null = null;

  constructor(projectRoot?: string) {
    this.projectRoot =
      projectRoot ?? resolve(import.meta.dirname ?? ".", "..");
  }

  async build(project?: string): Promise<BuildResult> {
    const start = Date.now();
    const makeTarget = project
      ? `-C ${project}`
      : "-C amiga-debug-lib && make -C examples/hello_world && make -C examples/bouncing_ball";

    const cmd = project
      ? ["run", "--rm", "-v", `${this.projectRoot}:/work`, "-w", "/work",
         DOCKER_IMAGE, "make", "-C", project]
      : ["run", "--rm", "-v", `${this.projectRoot}:/work`, "-w", "/work",
         DOCKER_IMAGE, "sh", "-c",
         "make -C amiga-debug-lib && make -C examples/hello_world && make -C examples/bouncing_ball"];

    try {
      const { stdout, stderr } = await execFileAsync("docker", cmd, {
        timeout: 120000,
      });

      this.lastBuildResult = {
        success: true,
        output: stdout,
        errors: stderr,
        duration: Date.now() - start,
      };
    } catch (err: unknown) {
      const error = err as { stdout?: string; stderr?: string; message?: string };
      this.lastBuildResult = {
        success: false,
        output: error.stdout ?? "",
        errors: error.stderr ?? error.message ?? "Unknown error",
        duration: Date.now() - start,
      };
    }

    return this.lastBuildResult;
  }

  async clean(project?: string): Promise<BuildResult> {
    const start = Date.now();
    const target = project ?? "amiga-debug-lib";

    try {
      const { stdout, stderr } = await execFileAsync(
        "docker",
        [
          "run", "--rm",
          "-v", `${this.projectRoot}:/work`,
          "-w", "/work",
          DOCKER_IMAGE,
          "make", "-C", target, "clean",
        ],
        { timeout: 60000 }
      );

      this.lastBuildResult = {
        success: true,
        output: stdout,
        errors: stderr,
        duration: Date.now() - start,
      };
    } catch (err: unknown) {
      const error = err as { stdout?: string; stderr?: string; message?: string };
      this.lastBuildResult = {
        success: false,
        output: error.stdout ?? "",
        errors: error.stderr ?? error.message ?? "Unknown error",
        duration: Date.now() - start,
      };
    }

    return this.lastBuildResult;
  }
}
