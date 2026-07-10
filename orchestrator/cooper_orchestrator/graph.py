from __future__ import annotations

from typing import Hashable

from langgraph.graph import END, START, StateGraph
from langgraph.graph.state import CompiledStateGraph

from cooper_orchestrator.agents import coder, product_manager, scheduler, tester
from cooper_orchestrator.config import Settings
from cooper_orchestrator.llm.client import LLMClient
from cooper_orchestrator.schemas import TaskState

def build_graph(llm: LLMClient, settings: Settings) -> CompiledStateGraph:
    def product_manager_node(state: TaskState) -> TaskState:
        return product_manager.run(state, llm, settings)

    def scheduler_node(state: TaskState) -> TaskState:
        return scheduler.run(state, llm, settings)

    def coder_node(state: TaskState) -> TaskState:
        return coder.run(state, llm, settings)

    def tester_node(state: TaskState) -> TaskState:
        return tester.run(state, llm, settings)

    def route_after_tester(state: TaskState) -> Hashable:
        if state.completed or state.failed:
            return END
        return "coder"

    graph = StateGraph(TaskState)
    graph.add_node("product_manager", product_manager_node)
    graph.add_node("scheduler", scheduler_node)
    graph.add_node("coder", coder_node)
    graph.add_node("tester", tester_node)

    graph.add_edge(START, "product_manager")
    graph.add_edge("product_manager", "scheduler")
    graph.add_edge("scheduler", "coder")
    graph.add_edge("coder", "tester")
    graph.add_conditional_edges("tester", route_after_tester)

    return graph.compile()

def run_pipeline(business_requirement: str, repo_path: str, llm: LLMClient, settings: Settings) -> TaskState:
    compiled_graph = build_graph(llm, settings)
    initial_state = TaskState(
        business_requirement=business_requirement,
        repo_path=repo_path,
        max_retries=settings.max_retries,
    )
    raw_result = compiled_graph.invoke(initial_state, config={"recursion_limit": 50})
    return TaskState.model_validate(raw_result)