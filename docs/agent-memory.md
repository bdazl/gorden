# Agent memory

Gorden's robot keeps a memory that outlives one conversation and one session.
It is `gorden.agent.memory` (`apps/gorden/src/agent/memory.cppm`), the first
implementation of the layers in [architecture](architecture.md#memory-is-a-first-class-concept).

## What is stored

| Layer | Type | Written by |
|---|---|---|
| Episodic memory | `Episode{id, text, think, at}` | the `remember` tool |
| Beliefs | `Belief{subject, predicate, value, source, learnedAt, confidence}` | the `believe` tool |
| Goals | `Goal{id, text, status, createdAt}` | the `setGoal` / `closeGoal` tools |

Working memory — the bounded chat history — stays in `AgentBrain` and is *not*
saved: after a load the robot resumes with what it chose to remember, not with
the raw conversation.

Nothing writes to memory behind the model's back. Every write comes from a tool
call that passed `validate()`, so the robot decides what is worth keeping while
the rules still decide what is allowed (text lengths, confidence in `[0,1]`, at
most five active goals). One `(subject, predicate)` holds one value: a new
`believe` revises the old claim instead of piling contradictions.

## What reaches the model

- **Every observation** carries the active goals (with their ids, so the model
  can close them) and one rendered line per belief. They are small and they
  steer the next decision.
- **Episodes only on request**, through `recall`. Retrieval scores an episode by
  how many words of the query it mentions and breaks ties by recency; the tool
  result is the matching episodes, so old memories enter the prompt only when
  the model asked for them. No embeddings — deliberately, until we know what the
  model actually needs.

Caps keep both the prompt and the save file bounded: 200 episodes, 100 beliefs,
50 goals, oldest first out.

## Persistence

Memory is serialised as `{version, next_id, episodes, beliefs, goals}` and
stored in the `app` object of a save game (see [the save format](save-format.md))
together with the robot and player transforms and the brain's simulation clock.
`next_id` travels with it so ids stay unique across a save and load.

## Inspecting it

`/proc/gorden/memory` in the robot's own filesystem dumps the whole memory as
JSON; `/var/log/agent.log` shows every write and every recall as it happens
(`remembered ep-3: ...`, `recall "crate" → 2 hit(s)`, `goal goal-4 done`).
