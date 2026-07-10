from __future__ import annotations

import argparse
import sys
from pathlib import Path

from cooper_orchestrator.config import Settings
from cooper_orchestrator.graph import run_pipeline
from cooper_orchestrator.llm.client import LiteLLMClient

def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="cooper_orchestrator")
    subparsers = parser.add_subparsers(dest="command", required=True)

    run_parser = subparsers.add_parser("run", help="Run the PM -> Scheduler -> Coder -> Tester pipeline")
    run_parser.add_argument("--repo", required=True, help="Path to the target git repository")
    run_parser.add_argument("--brd", required=True, help="Path to a text file containing the business requirement")

    return parser

def main(argv: list[str] | None = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)

    if args.command == "run":
        business_requirement = Path(args.brd).read_text()
        settings = Settings()
        llm = LiteLLMClient(settings)
        final_state = run_pipeline(business_requirement, args.repo, llm, settings)

        for line in final_state.history:
            print(line)

        if final_state.completed:
            print("Pipeline completed successfully.")
            return 0

        print("Pipeline did not complete successfully.")
        return 1

    return 1

if __name__ == "__main__":
    sys.exit(main())
