---
description: Guided work loop — explore, ask focused questions, then enter plan mode
argument-hint: short description of the task
---

# /work — guided exploration → questions → plan

Initial task: $ARGUMENTS

Run this loop. The goal is alignment before planning, not implementation.

## 1. Explore (only if needed)

Read enough of the codebase and `docs/` to ask informed questions. For a
trivial task, skip this step. For anything non-trivial, prefer parallel
`Agent` (Explore) calls for breadth and direct `Read` for known files.

Stop exploring as soon as you can name the real decisions. Don't keep
reading "just in case" — questions surface the rest.

## 2. Ask the load-bearing questions

Surface decisions that actually change what gets built: scope boundaries,
subsystem splits, naming, dependencies, error model, test strategy,
in-scope vs. out-of-scope, etc. Skip anything already settled by
`docs/decisions.md`, `docs/conventions/`, or obvious context.

For every question:

- **Use `AskUserQuestion`.** Never plain-text questions — the user wants a
  pickable TUI.
- **Recommendation first** in the option list, with `(Recommended)` in the
  label. Lead with what you'd choose; the others are alternatives.
- Each option's `description` names the trade-off in one short line, not
  what the option literally says.
- 2–4 options per question. The harness adds "Other" for free-form, so
  don't include your own.
- Batch related questions in a single `AskUserQuestion` call (up to 4)
  when they're independent. Ask sequentially when one answer should shape
  the next question.

If a decision is open-ended ("what should this be called?") and a
pick-list feels forced, still propose 2–3 concrete starting points — the
user can override via Other.

Don't pad. A question that wouldn't change what you build is noise.

## 3. Hand off to plan mode

When the open questions are answered, call `EnterPlanMode` directly. Do
not ask "ready to plan?" first — the user prefers automatic hand-off.

If `EnterPlanMode` isn't accessible for any reason, say one sentence:
"Enough context — flip to plan mode when ready."

## Don'ts

- Don't start implementing. This command is exploration + alignment only.
- Don't ask permission questions ("should I read more?", "ready to
  plan?"). Just act, or hand off.
- Don't summarize what you read back at the user before asking — the
  questions themselves prove you understood.
