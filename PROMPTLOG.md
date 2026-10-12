Episode 0 
Prompt:
I’m starting HW2 in this repository. Before making any changes, I want you to help me understand the existing codebase and develop a plan for the assignment.
For this first session, do not modify any files or implement anything.
Please work through these steps:
Read CLAUDE.md and the HW2 assignment instructions available in the repository. Treat them as the source of truth for the requirements and constraints.
Inspect the current repository structure, including src/rbtree.c, include/rbtree.h, the existing tests, and the Makefile. Check git status so we know whether there are existing changes that need to be preserved.
Explain how the current red-black tree is implemented. Focus on node allocation, key and value ownership, insertion and deletion, the nil sentinel, and how memory is freed.

Notes: Since I'm starting this Claude session in a new repository, I wanted to make sure that Claude understood my implementation for HW1. That is the purpose for this prompt. Claude caught some small things like my DEVLOG.md not being named correctly, and the starter makefile not being updated. It then told me how my implementation for hw1 works and a rough draft for the milestones for this HW.