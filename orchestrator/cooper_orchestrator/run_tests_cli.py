from __future__ import annotations

import json
import sys

from cooper_orchestrator.config import SandboxSettings
from cooper_orchestrator.sandbox.docker_sandbox import DockerSandbox

def main() -> int:
    payload_path = sys.argv[1]
    with open(payload_path, "r") as payload_file:
        payload = json.load(payload_file)

    settings = SandboxSettings(**payload["sandbox_settings"])
    sandbox = DockerSandbox(settings)
    result = sandbox.run(payload["repo_path"], payload["command"])

    print(
        json.dumps(
            {
                "passed": result.passed,
                "stdout": result.stdout,
                "stderr": result.stderr,
                "exit_code": result.exit_code,
            }
        )
    )
    return 0

if __name__ == "__main__":
    sys.exit(main())
