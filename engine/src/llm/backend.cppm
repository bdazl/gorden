// Primary interface unit for the LLM backends. One Provider interface
// (roboslop.llm), several concrete implementations as partitions — the
// case the module-layout rules reserve partitions for.
export module roboslop.llm.backend;

export import :scripted;
